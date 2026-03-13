/*
 * EXP-2: Native C IOKit HID PoC for Apple SPU IMU
 * =================================================
 * 彻底绕开 Python 封装，直接用 C 调用 IOKit HID API 读取 BMI286 IMU。
 * 测试 kIOHIDOptionsTypeNone (非独占) 是否允许非 root 打开 SPU 传感器。
 *
 * 编译:
 *   clang -o exp2_iokit_imu exp2_iokit_imu.c \
 *       -framework IOKit -framework CoreFoundation -Wall -O2
 *
 * 运行:
 *   ./exp2_iokit_imu              # 非 root
 *   sudo ./exp2_iokit_imu         # root 对照
 *
 * 判定标准:
 *   SUCCESS: "READING DATA" + 打印 x/y/z 值
 *   FAIL:    "IOHIDDeviceOpen failed" + 错误码
 *
 * 设备匹配: DeviceUsagePage=0xFF00, DeviceUsage=3 (加速度计)
 *           DeviceUsagePage=0xFF00, DeviceUsage=9 (陀螺仪)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

/* ── Globals ──────────────────────────────────────────────────── */
static volatile int g_running = 1;
static int g_sample_count = 0;
static int g_target_samples = 200;  /* ~2 sec @ 100Hz */
static CFRunLoopRef g_run_loop = NULL;

typedef struct {
    const char *name;       /* "accel" or "gyro" */
    int usage;              /* 3 or 9 */
    io_service_t service;
    IOHIDDeviceRef device;
    int open_result;        /* IOReturn from Open */
    int callback_count;
    double last_x, last_y, last_z;
    double scale;
} SensorCtx;

typedef struct {
    int usage_page;
    int usage;
    char product[128];
    char transport[128];
} ServiceInfo;

/* ── Signal handler ───────────────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_run_loop) CFRunLoopStop(g_run_loop);
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

static int get_registry_int_property(io_registry_entry_t service, CFStringRef key) {
    int32_t value = 0;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!ref) return -1;
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
    if (!ref) return;
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
    if (kr != KERN_SUCCESS) return;

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int32_t enabled = 1;
        int32_t interval = 5000;
        CFNumberRef enabled_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &enabled);
        CFNumberRef interval_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &interval);
        IORegistryEntrySetCFProperty(service, CFSTR("SensorPropertyReportingState"), enabled_num);
        IORegistryEntrySetCFProperty(service, CFSTR("SensorPropertyPowerState"), enabled_num);
        IORegistryEntrySetCFProperty(service, CFSTR("ReportInterval"), interval_num);
        CFRelease(enabled_num);
        CFRelease(interval_num);
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
}

/* ── HID input report callback ────────────────────────────────── */
static void input_callback(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t reportID,
    uint8_t *report,
    CFIndex reportLength
) {
    (void)result; (void)sender; (void)type; (void)reportID;
    SensorCtx *ctx = (SensorCtx *)context;

    if (reportLength < 18) return;

    /*
     * BMI286 report format (22 bytes):
     *   bytes 0-5:   header
     *   bytes 6-9:   X axis (int32_t LE)
     *   bytes 10-13: Y axis (int32_t LE)
     *   bytes 14-17: Z axis (int32_t LE)
     *   bytes 18-21: additional data
     */
    int32_t raw_x, raw_y, raw_z;
    memcpy(&raw_x, report + 6, 4);
    memcpy(&raw_y, report + 10, 4);
    memcpy(&raw_z, report + 14, 4);

    ctx->last_x = (double)raw_x / ctx->scale;
    ctx->last_y = (double)raw_y / ctx->scale;
    ctx->last_z = (double)raw_z / ctx->scale;
    ctx->callback_count++;

    if (ctx->callback_count <= 5 || ctx->callback_count % 50 == 0) {
        printf("  [%s] sample #%d: x=%.4f y=%.4f z=%.4f\n",
               ctx->name, ctx->callback_count,
               ctx->last_x, ctx->last_y, ctx->last_z);
    }

    g_sample_count++;
    if (g_sample_count >= g_target_samples) {
        g_running = 0;
        if (g_run_loop) CFRunLoopStop(g_run_loop);
    }
}

/* ── Find SPU sensor service using registry properties ────────── */
static io_service_t find_spu_service(int usage, ServiceInfo *info_out) {
    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleSPUHIDDevice"),
        &iter
    );
    if (kr != KERN_SUCCESS) return IO_OBJECT_NULL;

    io_service_t found = IO_OBJECT_NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int up = get_registry_int_property(service, CFSTR("PrimaryUsagePage"));
        int u = get_registry_int_property(service, CFSTR("PrimaryUsage"));
        if (up == 0xFF00 && u == usage) {
            if (info_out) {
                memset(info_out, 0, sizeof(*info_out));
                info_out->usage_page = up;
                info_out->usage = u;
                get_registry_string_property(service, CFSTR("Product"),
                                             info_out->product, sizeof(info_out->product));
                get_registry_string_property(service, CFSTR("Transport"),
                                             info_out->transport, sizeof(info_out->transport));
            }
            found = service;
            break;
        }
        IOObjectRelease(service);
    }

    IOObjectRelease(iter);
    return found;
}

/* ── Test one open mode ───────────────────────────────────────── */
static int test_open_mode(
    IOHIDDeviceRef device,
    IOOptionBits options,
    const char *mode_name,
    SensorCtx *ctx
) {
    printf("  Trying IOHIDDeviceOpen(%s, %s)...\n", ctx->name, mode_name);

    IOReturn ret = IOHIDDeviceOpen(device, options);
    ctx->open_result = (int)ret;

    if (ret == kIOReturnSuccess) {
        printf("  ✓ IOHIDDeviceOpen(%s) = kIOReturnSuccess (0x%08x)\n",
               mode_name, ret);
        return 1;
    } else {
        printf("  ✗ IOHIDDeviceOpen(%s) = 0x%08x", mode_name, ret);
        if (ret == (IOReturn)0xe00002e2)
            printf(" (kIOReturnNotPermitted)");
        else if (ret == (IOReturn)0xe00002c5)
            printf(" (kIOReturnExclusiveAccess)");
        printf("\n");
        return 0;
    }
}

/* ── Main ─────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("═══════════════════════════════════════════════════════\n");
    printf("  EXP-2: Native C IOKit HID PoC\n");
    printf("  euid: %d\n", geteuid());
    printf("  is_root: %s\n", geteuid() == 0 ? "true" : "false");
    print_macos_metadata();
    printf("═══════════════════════════════════════════════════════\n\n");

    /* Create HID Manager */
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, 0);
    if (!manager) {
        fprintf(stderr, "  FAIL: Cannot create IOHIDManager\n");
        return 1;
    }

    /* Match vendor page 0xFF00 */
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    int32_t page = 0xFF00;
    CFNumberRef pageNum = CFNumberCreate(kCFAllocatorDefault,
                                         kCFNumberSInt32Type, &page);
    CFDictionarySetValue(match, CFSTR(kIOHIDDeviceUsagePageKey), pageNum);
    IOHIDManagerSetDeviceMatching(manager, match);

    /* Open manager for comparison only. On Tahoe this may be denied even if
     * direct IOService + IOHIDDeviceOpen still works, so do not abort here. */
    IOReturn mr = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    printf("  IOHIDManagerOpen(None) = 0x%08x %s\n\n",
           mr, mr == kIOReturnSuccess ? "✓" : "✗");
    if (mr != kIOReturnSuccess) {
        fprintf(stderr, "  WARN: IOHIDManagerOpen failed, continuing with direct service path\n");
    }

    wake_spu_drivers();

    /* Find accelerometer (usage 3) and gyroscope (usage 9) */
    SensorCtx accel = { .name = "accel", .usage = 3, .scale = 65536.0 };
    SensorCtx gyro  = { .name = "gyro",  .usage = 9, .scale = 65536.0 };
    ServiceInfo accel_info = {0};
    ServiceInfo gyro_info = {0};

    accel.service = find_spu_service(3, &accel_info);
    gyro.service = find_spu_service(9, &gyro_info);

    if (accel.service) {
        accel.device = IOHIDDeviceCreate(kCFAllocatorDefault, accel.service);
    }
    if (gyro.service) {
        gyro.device = IOHIDDeviceCreate(kCFAllocatorDefault, gyro.service);
    }

    printf("  Accelerometer (usage 3): %s\n", accel.device ? "FOUND" : "NOT FOUND");
    if (accel.device) {
        printf("    transport=%s product=%s\n",
               accel_info.transport[0] ? accel_info.transport : "(null)",
               accel_info.product[0] ? accel_info.product : "(null)");
    }
    printf("  Gyroscope     (usage 9): %s\n", gyro.device ? "FOUND" : "NOT FOUND");
    if (gyro.device) {
        printf("    transport=%s product=%s\n",
               gyro_info.transport[0] ? gyro_info.transport : "(null)",
               gyro_info.product[0] ? gyro_info.product : "(null)");
    }
    printf("\n");

    if (!accel.device && !gyro.device) {
        printf("  ✗ No SPU sensor found. Device may not be supported.\n");
        IOHIDManagerClose(manager, 0);
        CFRelease(match);
        CFRelease(pageNum);
        return 1;
    }

    /* ── Test Phase A: Try kIOHIDOptionsTypeNone (non-exclusive) ── */
    printf("━━━ Phase A: kIOHIDOptionsTypeNone (non-exclusive) ━━━\n");
    int accel_open = 0, gyro_open = 0;

    if (accel.device) {
        accel_open = test_open_mode(accel.device,
            kIOHIDOptionsTypeNone, "kIOHIDOptionsTypeNone", &accel);
    }
    if (gyro.device) {
        gyro_open = test_open_mode(gyro.device,
            kIOHIDOptionsTypeNone, "kIOHIDOptionsTypeNone", &gyro);
    }
    printf("\n");

    /* If non-exclusive failed, try exclusive (seize) for comparison */
    if (!accel_open && accel.device) {
        printf("━━━ Phase B: kIOHIDOptionsTypeSeizeDevice (exclusive) ━━━\n");
        test_open_mode(accel.device,
            kIOHIDOptionsTypeSeizeDevice, "kIOHIDOptionsTypeSeizeDevice", &accel);
        printf("\n");
    }

    /* ── If open succeeded, register callback and read data ──── */
    if (accel_open || gyro_open) {
        printf("━━━ READING DATA (target: %d samples) ━━━\n", g_target_samples);

        g_run_loop = CFRunLoopGetCurrent();

        if (accel_open) {
            uint8_t *buf = calloc(64, 1);
            IOHIDDeviceRegisterInputReportCallback(
                accel.device, buf, 64, input_callback, &accel);
            IOHIDDeviceScheduleWithRunLoop(
                accel.device, g_run_loop, kCFRunLoopDefaultMode);
        }
        if (gyro_open) {
            uint8_t *buf = calloc(64, 1);
            IOHIDDeviceRegisterInputReportCallback(
                gyro.device, buf, 64, input_callback, &gyro);
            IOHIDDeviceScheduleWithRunLoop(
                gyro.device, g_run_loop, kCFRunLoopDefaultMode);
        }

        /* Run for up to 5 seconds */
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5.0, false);

        printf("\n  Accel callbacks: %d\n", accel.callback_count);
        printf("  Gyro callbacks:  %d\n", gyro.callback_count);
    }

    /* ── Summary ──────────────────────────────────────────────── */
    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  SUMMARY\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  euid:                %d\n", geteuid());
    printf("  accel found:         %s\n", accel.device ? "yes" : "no");
    printf("  accel open(None):    0x%08x %s\n",
           accel.open_result,
           accel.open_result == 0 ? "SUCCESS" : "FAIL");
    printf("  accel samples:       %d\n", accel.callback_count);
    printf("  gyro found:          %s\n", gyro.device ? "yes" : "no");
    printf("  gyro open(None):     0x%08x %s\n",
           gyro.open_result,
           gyro.open_result == 0 ? "SUCCESS" : "FAIL");
    printf("  gyro samples:        %d\n", gyro.callback_count);

    int total = accel.callback_count + gyro.callback_count;
    if (total > 0 && geteuid() != 0) {
        printf("\n  ★ CRITICAL FINDING: Non-root IMU read SUCCEEDED!\n");
        printf("    This means H1 is CONFIRMED: root is NOT required by the OS.\n");
    } else if (total > 0 && geteuid() == 0) {
        printf("\n  ✓ Root read succeeded (expected baseline).\n");
    } else if (total == 0 && geteuid() != 0) {
        printf("\n  ✗ Non-root read FAILED.\n");
        printf("    Check open_result codes above to determine cause.\n");
    }
    printf("═══════════════════════════════════════════════════════\n");

    /* Cleanup */
    if (accel.device) { IOHIDDeviceClose(accel.device, 0); CFRelease(accel.device); }
    if (gyro.device)  { IOHIDDeviceClose(gyro.device, 0);  CFRelease(gyro.device); }
    if (accel.service) IOObjectRelease(accel.service);
    if (gyro.service)  IOObjectRelease(gyro.service);
    IOHIDManagerClose(manager, 0);
    CFRelease(match);
    CFRelease(pageNum);

    return (total > 0) ? 0 : 1;
}
