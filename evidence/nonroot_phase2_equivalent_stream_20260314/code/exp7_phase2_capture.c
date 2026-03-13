#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static CFRunLoopRef g_run_loop = NULL;
static FILE *g_csv = NULL;
static uint64_t g_first_ts_ns = 0;
static uint64_t g_last_ts_ns = 0;
static size_t g_total_rows = 0;
static int g_print_budget = 10;
static bool g_quiet = false;
struct SensorCtx;
static struct SensorCtx *g_accel_ctx = NULL;
static struct SensorCtx *g_gyro_ctx = NULL;

typedef struct SensorCtx {
    const char *name;
    int usage;
    io_service_t service;
    IOHIDDeviceRef device;
    IOReturn open_result;
    int callback_count;
    double x;
    double y;
    double z;
    bool have_value;
    uint8_t report_buf[4096];
} SensorCtx;

typedef struct {
    int duration_sec;
    const char *csv_path;
    int emit_hz;
    bool quiet;
} Options;

static uint64_t monotonic_ns(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_run_loop) {
        CFRunLoopStop(g_run_loop);
    }
}

static void print_macos_metadata(void) {
    FILE *fp = popen("sw_vers -productVersion", "r");
    char version[64] = "unknown";
    char build[64] = "unknown";

    if (fp) {
        if (fgets(version, sizeof(version), fp)) {
            version[strcspn(version, "\n")] = '\0';
        }
        pclose(fp);
    }

    fp = popen("sw_vers -buildVersion", "r");
    if (fp) {
        if (fgets(build, sizeof(build), fp)) {
            build[strcspn(build, "\n")] = '\0';
        }
        pclose(fp);
    }

    printf("  macOS: %s (%s)\n", version, build);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --csv <path> [--seconds N] [--quiet]\n"
            "  Writes Phase-2-compatible sensor CSV using the non-root direct SPU path.\n",
            argv0);
}

static bool parse_args(int argc, char **argv, Options *opts) {
    opts->duration_sec = 10;
    opts->csv_path = NULL;
    opts->emit_hz = 200;
    opts->quiet = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            opts->duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--emit-hz") == 0 && i + 1 < argc) {
            opts->emit_hz = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            opts->csv_path = argv[++i];
        } else if (strcmp(argv[i], "--quiet") == 0) {
            opts->quiet = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return false;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return false;
        }
    }

    if (!opts->csv_path || opts->duration_sec <= 0 || opts->emit_hz <= 0) {
        usage(argv[0]);
        return false;
    }

    return true;
}

static int get_registry_int_property(io_registry_entry_t service, CFStringRef key) {
    int32_t value = 0;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!ref) {
        return -1;
    }
    if (CFGetTypeID(ref) == CFNumberGetTypeID() &&
        CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &value)) {
        CFRelease(ref);
        return value;
    }
    CFRelease(ref);
    return -1;
}

static void get_registry_string_property(
    io_registry_entry_t service,
    CFStringRef key,
    char *out,
    size_t out_len
) {
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!ref) {
        return;
    }
    if (CFGetTypeID(ref) == CFStringGetTypeID()) {
        CFStringGetCString((CFStringRef)ref, out, out_len, kCFStringEncodingUTF8);
    }
    CFRelease(ref);
}

static void wake_spu_drivers(void) {
    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleSPUHIDDriver"),
        &iter
    );
    if (kr != KERN_SUCCESS) {
        return;
    }

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int32_t enabled = 1;
        int32_t interval = 5000;
        CFNumberRef enabled_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &enabled);
        CFNumberRef interval_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &interval);
        if (enabled_num) {
            IORegistryEntrySetCFProperty(service, CFSTR("SensorPropertyReportingState"), enabled_num);
            IORegistryEntrySetCFProperty(service, CFSTR("SensorPropertyPowerState"), enabled_num);
            CFRelease(enabled_num);
        }
        if (interval_num) {
            IORegistryEntrySetCFProperty(service, CFSTR("ReportInterval"), interval_num);
            CFRelease(interval_num);
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
}

static io_service_t find_spu_service(int usage, char *transport, size_t transport_len) {
    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleSPUHIDDevice"),
        &iter
    );
    if (kr != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }

    io_service_t found = IO_OBJECT_NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int up = get_registry_int_property(service, CFSTR("PrimaryUsagePage"));
        int u = get_registry_int_property(service, CFSTR("PrimaryUsage"));
        if (up == 0xFF00 && u == usage) {
            if (transport && transport_len > 0) {
                memset(transport, 0, transport_len);
                get_registry_string_property(service, CFSTR("Transport"), transport, transport_len);
            }
            found = service;
            break;
        }
        IOObjectRelease(service);
    }

    IOObjectRelease(iter);
    return found;
}

static void write_row(bool quiet, const char *source, const SensorCtx *accel, const SensorCtx *gyro) {
    uint64_t ts_ns = monotonic_ns();
    double ax = accel->have_value ? accel->x : 0.0;
    double ay = accel->have_value ? accel->y : 0.0;
    double az = accel->have_value ? accel->z : 0.0;
    double gx = gyro->have_value ? gyro->x : 0.0;
    double gy = gyro->have_value ? gyro->y : 0.0;
    double gz = gyro->have_value ? gyro->z : 0.0;

    if (!g_first_ts_ns) {
        g_first_ts_ns = ts_ns;
    }
    g_last_ts_ns = ts_ns;
    g_total_rows++;

    fprintf(g_csv,
            "%llu,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f\n",
            (unsigned long long)ts_ns,
            ax, ay, az, gx, gy, gz);

    if ((g_total_rows % 256) == 0) {
        fflush(g_csv);
    }

    if (!quiet && g_print_budget > 0) {
        printf("  [%s] row #%zu ts=%llu ax=%.4f ay=%.4f az=%.4f gx=%.4f gy=%.4f gz=%.4f\n",
               source,
               g_total_rows,
               (unsigned long long)ts_ns,
               ax, ay, az, gx, gy, gz);
        g_print_budget--;
    }
}

static void input_callback(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t reportID,
    uint8_t *report,
    CFIndex reportLength
) {
    (void)result;
    (void)sender;
    (void)type;
    (void)reportID;

    SensorCtx *ctx = (SensorCtx *)context;
    if (!g_running || reportLength < 18 || !report) {
        return;
    }

    int32_t raw_x = 0;
    int32_t raw_y = 0;
    int32_t raw_z = 0;
    memcpy(&raw_x, report + 6, 4);
    memcpy(&raw_y, report + 10, 4);
    memcpy(&raw_z, report + 14, 4);

    ctx->x = (double)raw_x / 65536.0;
    ctx->y = (double)raw_y / 65536.0;
    ctx->z = (double)raw_z / 65536.0;
    ctx->have_value = true;
    ctx->callback_count++;
}

static bool setup_sensor(SensorCtx *ctx, char *transport, size_t transport_len) {
    ctx->service = find_spu_service(ctx->usage, transport, transport_len);
    if (ctx->service == IO_OBJECT_NULL) {
        printf("  %s (usage %d): NOT FOUND\n", ctx->name, ctx->usage);
        return false;
    }

    printf("  %s (usage %d): FOUND transport=%s\n",
           ctx->name, ctx->usage, transport[0] ? transport : "(null)");

    ctx->device = IOHIDDeviceCreate(kCFAllocatorDefault, ctx->service);
    if (!ctx->device) {
        printf("  %s: IOHIDDeviceCreate failed\n", ctx->name);
        return false;
    }

    ctx->open_result = IOHIDDeviceOpen(ctx->device, kIOHIDOptionsTypeNone);
    if (ctx->open_result != kIOReturnSuccess) {
        printf("  %s: IOHIDDeviceOpen(None) failed: 0x%08x\n",
               ctx->name, ctx->open_result);
        return false;
    }

    IOHIDDeviceRegisterInputReportCallback(
        ctx->device,
        ctx->report_buf,
        sizeof(ctx->report_buf),
        input_callback,
        ctx
    );
    IOHIDDeviceScheduleWithRunLoop(ctx->device, g_run_loop, kCFRunLoopDefaultMode);
    return true;
}

static void teardown_sensor(SensorCtx *ctx) {
    if (ctx->device) {
        IOHIDDeviceUnscheduleFromRunLoop(ctx->device, g_run_loop, kCFRunLoopDefaultMode);
        IOHIDDeviceClose(ctx->device, kIOHIDOptionsTypeNone);
        CFRelease(ctx->device);
        ctx->device = NULL;
    }
    if (ctx->service != IO_OBJECT_NULL) {
        IOObjectRelease(ctx->service);
        ctx->service = IO_OBJECT_NULL;
    }
}

int main(int argc, char **argv) {
    Options opts;
    if (!parse_args(argc, argv, &opts)) {
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    g_csv = fopen(opts.csv_path, "w");
    if (!g_csv) {
        perror("fopen(csv)");
        return 1;
    }
    fprintf(g_csv, "timestamp_ns,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z\n");

    SensorCtx accel = {.name = "accel", .usage = 3};
    SensorCtx gyro = {.name = "gyro", .usage = 9};
    char accel_transport[128] = {0};
    char gyro_transport[128] = {0};
    IOHIDManagerRef manager = NULL;
    CFMutableDictionaryRef match = NULL;
    CFNumberRef page_num = NULL;

    printf("═══════════════════════════════════════════════════════\n");
    printf("  EXP-7: Phase-2 Compatibility Capture Probe\n");
    printf("  euid: %d\n", geteuid());
    printf("  is_root: %s\n", geteuid() == 0 ? "true" : "false");
    print_macos_metadata();
    printf("  duration: %d sec\n", opts.duration_sec);
    printf("  emit rate: %d Hz\n", opts.emit_hz);
    printf("  csv: %s\n", opts.csv_path);
    printf("═══════════════════════════════════════════════════════\n\n");

    g_run_loop = CFRunLoopGetCurrent();
    g_quiet = opts.quiet;
    g_accel_ctx = &accel;
    g_gyro_ctx = &gyro;

    manager = IOHIDManagerCreate(kCFAllocatorDefault, 0);
    if (!manager) {
        fprintf(stderr, "  FAIL: Cannot create IOHIDManager\n");
        fclose(g_csv);
        return 1;
    }

    match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    int32_t page = 0xFF00;
    page_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &page);
    if (match && page_num) {
        CFDictionarySetValue(match, CFSTR(kIOHIDDeviceUsagePageKey), page_num);
        IOHIDManagerSetDeviceMatching(manager, match);
        IOReturn mr = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
        printf("  IOHIDManagerOpen(None) = 0x%08x %s\n\n",
               mr, mr == kIOReturnSuccess ? "✓" : "✗");
    }

    wake_spu_drivers();

    if (!setup_sensor(&accel, accel_transport, sizeof(accel_transport))) {
        teardown_sensor(&accel);
        teardown_sensor(&gyro);
        if (manager) {
            IOHIDManagerClose(manager, 0);
            CFRelease(manager);
        }
        if (match) {
            CFRelease(match);
        }
        if (page_num) {
            CFRelease(page_num);
        }
        fclose(g_csv);
        return 2;
    }

    if (!setup_sensor(&gyro, gyro_transport, sizeof(gyro_transport))) {
        teardown_sensor(&accel);
        teardown_sensor(&gyro);
        if (manager) {
            IOHIDManagerClose(manager, 0);
            CFRelease(manager);
        }
        if (match) {
            CFRelease(match);
        }
        if (page_num) {
            CFRelease(page_num);
        }
        fclose(g_csv);
        return 3;
    }

    uint64_t start_ns = monotonic_ns();
    uint64_t end_ns = start_ns + (uint64_t)opts.duration_sec * 1000000000ULL;
    uint64_t emit_interval_ns = 1000000000ULL / (uint64_t)opts.emit_hz;
    uint64_t next_emit_ns = start_ns;
    bool emit_started = false;

    while (g_running && monotonic_ns() < end_ns) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);
        uint64_t now_ns = monotonic_ns();
        if ((accel.have_value || gyro.have_value) && !emit_started) {
            emit_started = true;
            next_emit_ns = now_ns;
        }
        if (emit_started && now_ns >= next_emit_ns) {
            write_row(opts.quiet, "emit", &accel, &gyro);
            next_emit_ns += emit_interval_ns;
            while (next_emit_ns + emit_interval_ns <= now_ns) {
                next_emit_ns += emit_interval_ns;
            }
        }
    }

    fflush(g_csv);
    teardown_sensor(&accel);
    teardown_sensor(&gyro);
    if (manager) {
        IOHIDManagerClose(manager, 0);
        CFRelease(manager);
    }
    if (match) {
        CFRelease(match);
    }
    if (page_num) {
        CFRelease(page_num);
    }
    fclose(g_csv);

    double duration_sec = 0.0;
    if (g_first_ts_ns && g_last_ts_ns && g_last_ts_ns > g_first_ts_ns) {
        duration_sec = (double)(g_last_ts_ns - g_first_ts_ns) / 1e9;
    }

    double total_hz = (duration_sec > 0.0) ? ((double)g_total_rows / duration_sec) : 0.0;
    double accel_hz = (duration_sec > 0.0) ? ((double)accel.callback_count / duration_sec) : 0.0;
    double gyro_hz = (duration_sec > 0.0) ? ((double)gyro.callback_count / duration_sec) : 0.0;

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  SUMMARY\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  rows written:        %zu\n", g_total_rows);
    printf("  accel callbacks:     %d\n", accel.callback_count);
    printf("  gyro callbacks:      %d\n", gyro.callback_count);
    printf("  measured duration:   %.3f sec\n", duration_sec);
    printf("  total row rate:      %.2f Hz\n", total_hz);
    printf("  accel callback rate: %.2f Hz\n", accel_hz);
    printf("  gyro callback rate:  %.2f Hz\n", gyro_hz);
    printf("  csv schema:          timestamp_ns + 6-axis IMU\n");
    printf("═══════════════════════════════════════════════════════\n");

    return 0;
}
