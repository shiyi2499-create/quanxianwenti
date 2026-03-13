/*
 * EXP-6: IOHIDEventSystemClient probe for Sequoia IMU access
 * ==========================================================
 * Try a different access path from IOHIDDeviceOpen(...):
 *   - IOHIDEventSystemClientCreateSimpleClient()
 *   - IOHIDEventSystemClientCopyServices()
 *   - IOHIDServiceClientCopyEvent(...)
 *
 * This experiment is Sequoia-specific and intentionally isolated from the
 * Tahoe root-level baseline. The goal is to answer:
 *
 *   If IOHIDDeviceOpen is blocked on macOS 15.5, does the HID event-system
 *   path still expose accelerometer / gyro events to a non-root client?
 *
 * Build:
 *   clang -o exp6_event_system_probe exp6_event_system_probe.c \
 *       -framework IOKit -framework CoreFoundation -Wall -O2
 *
 * Run:
 *   ./exp6_event_system_probe
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>
#include <IOKit/hidsystem/IOHIDServiceClient.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDDeviceKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/IOReturn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct __IOHIDEvent *IOHIDEventRef;
typedef uint32_t IOHIDEventField;

/*
 * Apple exposes IOHIDServiceClientCopyEvent / IOHIDEventGetFloatValue in
 * the IOKit symbol table, but not in the public headers we have locally.
 * We declare the minimal signatures needed for this probe.
 */
extern IOHIDEventRef IOHIDServiceClientCopyEvent(
    IOHIDServiceClientRef service,
    int64_t event_type,
    int32_t options,
    int64_t timestamp
);
extern double IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);
extern IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
extern IOHIDEventSystemClientRef IOHIDEventSystemClientCreateWithType(CFAllocatorRef allocator, int type);
extern void IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client, CFDictionaryRef matching);
extern void IOHIDEventSystemClientScheduleWithRunLoop(
    IOHIDEventSystemClientRef client,
    CFRunLoopRef run_loop,
    CFStringRef run_loop_mode
);

#define IOHIDEventFieldBase(type) ((type) << 16)

enum {
    kIOHIDEventTypeNULL = 0,
    kIOHIDEventTypeVendorDefined = 1,
    kIOHIDEventTypeButton = 2,
    kIOHIDEventTypeKeyboard = 3,
    kIOHIDEventTypeTranslation = 4,
    kIOHIDEventTypeRotation = 5,
    kIOHIDEventTypeScroll = 6,
    kIOHIDEventTypeScale = 7,
    kIOHIDEventTypeZoom = 8,
    kIOHIDEventTypeVelocity = 9,
    kIOHIDEventTypeOrientation = 10,
    kIOHIDEventTypeDigitizer = 11,
    kIOHIDEventTypeAmbientLightSensor = 12,
    kIOHIDEventTypeAccelerometer = 13,
    kIOHIDEventTypeProximity = 14,
    kIOHIDEventTypeTemperature = 15,
    kIOHIDEventTypeNavigationSwipe = 16,
    kIOHIDEventTypePointer = 17,
    kIOHIDEventTypeProgress = 18,
    kIOHIDEventTypeMultiAxisPointer = 19,
    kIOHIDEventTypeGyro = 20,
    kIOHIDEventTypeCompass = 21,
    kIOHIDEventTypeZoomToggle = 22,
    kIOHIDEventTypeDockSwipe = 23,
    kIOHIDEventTypeSymbolicHotKey = 24,
    kIOHIDEventTypePower = 25,
    kIOHIDEventTypeLED = 26,
    kIOHIDEventTypeFluidTouchGesture = 27,
    kIOHIDEventTypeBoundaryScroll = 28,
    kIOHIDEventTypeBiometric = 29,
    kIOHIDEventTypeUnicode = 30,
    kIOHIDEventTypeAtmosphericPressure = 31,
};

enum {
    kIOHIDEventFieldAccelerometerX = IOHIDEventFieldBase(kIOHIDEventTypeAccelerometer),
    kIOHIDEventFieldAccelerometerY,
    kIOHIDEventFieldAccelerometerZ,
};

enum {
    kIOHIDEventFieldGyroX = IOHIDEventFieldBase(kIOHIDEventTypeGyro),
    kIOHIDEventFieldGyroY,
    kIOHIDEventFieldGyroZ,
};

typedef struct {
    int usage;
    const char *label;
    int expected_event_type;
    int x_field;
    int y_field;
    int z_field;
} SensorTarget;

static const SensorTarget kTargets[] = {
    {3, "accel", kIOHIDEventTypeAccelerometer,
     kIOHIDEventFieldAccelerometerX,
     kIOHIDEventFieldAccelerometerY,
     kIOHIDEventFieldAccelerometerZ},
    {9, "gyro", kIOHIDEventTypeGyro,
     kIOHIDEventFieldGyroX,
     kIOHIDEventFieldGyroY,
     kIOHIDEventFieldGyroZ},
};

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

static char *cfstring_copy_utf8(CFStringRef str) {
    if (!str) return NULL;
    CFIndex len = CFStringGetLength(str);
    CFIndex max_size = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    char *buffer = (char *)calloc((size_t)max_size, 1);
    if (!buffer) return NULL;
    if (!CFStringGetCString(str, buffer, max_size, kCFStringEncodingUTF8)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int cfnumber_to_int(CFTypeRef value, int fallback) {
    int32_t out = fallback;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
        if (CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &out)) {
            return out;
        }
    }
    return fallback;
}

static bool cfboolean_to_bool(CFTypeRef value, bool fallback) {
    if (value && CFGetTypeID(value) == CFBooleanGetTypeID()) {
        return CFBooleanGetValue((CFBooleanRef)value);
    }
    return fallback;
}

static char *copy_service_string(IOHIDServiceClientRef service, CFStringRef key) {
    CFTypeRef value = IOHIDServiceClientCopyProperty(service, key);
    char *result = NULL;
    if (value && CFGetTypeID(value) == CFStringGetTypeID()) {
        result = cfstring_copy_utf8((CFStringRef)value);
    }
    if (value) CFRelease(value);
    return result;
}

static int copy_service_int(IOHIDServiceClientRef service, CFStringRef key, int fallback) {
    CFTypeRef value = IOHIDServiceClientCopyProperty(service, key);
    int result = cfnumber_to_int(value, fallback);
    if (value) CFRelease(value);
    return result;
}

static bool copy_service_bool(IOHIDServiceClientRef service, CFStringRef key, bool fallback) {
    CFTypeRef value = IOHIDServiceClientCopyProperty(service, key);
    bool result = cfboolean_to_bool(value, fallback);
    if (value) CFRelease(value);
    return result;
}

static void set_sensor_intervals(IOHIDServiceClientRef service) {
    int32_t interval_us = 5000;
    CFNumberRef interval = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &interval_us);
    if (!interval) return;

    Boolean a = IOHIDServiceClientSetProperty(service, CFSTR(kIOHIDReportIntervalKey), interval);
    Boolean b = IOHIDServiceClientSetProperty(service, CFSTR(kIOHIDSampleIntervalKey), interval);
    Boolean c = IOHIDServiceClientSetProperty(service, CFSTR(kIOHIDSensorPropertyReportIntervalKey), interval);
    Boolean d = IOHIDServiceClientSetProperty(service, CFSTR(kIOHIDSensorPropertySampleIntervalKey), interval);

    printf("    interval request: Report=%s Sample=%s SensorReport=%s SensorSample=%s\n",
           a ? "ok" : "fail",
           b ? "ok" : "fail",
           c ? "ok" : "fail",
           d ? "ok" : "fail");

    CFRelease(interval);
}

static void print_event_description(IOHIDEventRef event) {
    CFStringRef description = CFCopyDescription(event);
    char *utf8 = NULL;
    if (description) {
        utf8 = cfstring_copy_utf8(description);
        CFRelease(description);
    }
    if (utf8) {
        printf("      description: %s\n", utf8);
        free(utf8);
    }
}

static bool try_known_event(
    IOHIDServiceClientRef service,
    const SensorTarget *target,
    int attempt_count
) {
    bool got_any = false;
    printf("    known event type %d (%s):\n", target->expected_event_type, target->label);

    for (int i = 0; i < attempt_count; i++) {
        IOHIDEventRef event = IOHIDServiceClientCopyEvent(service, target->expected_event_type, 0, 0);
        if (!event) {
            printf("      attempt %d: no event\n", i + 1);
            usleep(20000);
            continue;
        }

        double x = IOHIDEventGetFloatValue(event, target->x_field);
        double y = IOHIDEventGetFloatValue(event, target->y_field);
        double z = IOHIDEventGetFloatValue(event, target->z_field);
        printf("      attempt %d: x=%f y=%f z=%f\n", i + 1, x, y, z);
        print_event_description(event);
        CFRelease(event);
        got_any = true;
        usleep(20000);
    }

    return got_any;
}

static int sweep_event_types(IOHIDServiceClientRef service, int min_type, int max_type) {
    int hits = 0;
    printf("    sweeping event types [%d, %d]\n", min_type, max_type);
    for (int type = min_type; type <= max_type; type++) {
        IOHIDEventRef event = IOHIDServiceClientCopyEvent(service, type, 0, 0);
        if (!event) {
            continue;
        }

        printf("      hit: type=%d\n", type);
        print_event_description(event);
        CFRelease(event);
        hits++;
    }
    return hits;
}

static const SensorTarget *target_for_usage(int usage) {
    size_t count = sizeof(kTargets) / sizeof(kTargets[0]);
    for (size_t i = 0; i < count; i++) {
        if (kTargets[i].usage == usage) return &kTargets[i];
    }
    return NULL;
}

static CFDictionaryRef create_spu_matching_dict(void) {
    const void *keys[] = {
        CFSTR(kIOHIDTransportKey),
    };
    const void *values[] = {
        CFSTR("SPU"),
    };
    return CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
}

static CFArrayRef copy_services_with_strategy(
    const char *label,
    IOHIDEventSystemClientRef client,
    bool set_matching,
    bool schedule
) {
    CFDictionaryRef matching = NULL;
    if (set_matching) {
        matching = create_spu_matching_dict();
        if (matching) {
            IOHIDEventSystemClientSetMatching(client, matching);
        }
    }

    if (schedule) {
        IOHIDEventSystemClientScheduleWithRunLoop(client, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }

    CFArrayRef services = IOHIDEventSystemClientCopyServices(client);
    printf("  client strategy %-22s -> %s\n",
           label,
           services ? "services visible" : "NULL");

    if (matching) CFRelease(matching);
    return services;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    int forced_client_type = -1;
    if (argc == 3 && strcmp(argv[1], "--client-type") == 0) {
        forced_client_type = atoi(argv[2]);
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--client-type N]\n", argv[0]);
        return 2;
    }

    printf("═══════════════════════════════════════════════════════\n");
    printf("  EXP-6: IOHIDEventSystemClient Motion Probe\n");
    printf("  euid: %d\n", geteuid());
    printf("  is_root: %s\n", geteuid() == 0 ? "true" : "false");
    print_macos_metadata();
    if (forced_client_type >= 0) {
        printf("  forced client type: %d\n", forced_client_type);
    }
    printf("═══════════════════════════════════════════════════════\n\n");

    IOHIDEventSystemClientRef client = NULL;
    CFArrayRef services = NULL;

    if (forced_client_type >= 0) {
        printf("  calling IOHIDEventSystemClientCreateWithType(%d)\n", forced_client_type);
        client = IOHIDEventSystemClientCreateWithType(kCFAllocatorDefault, forced_client_type);
        printf("  createWithType returned: %s\n", client ? "non-NULL" : "NULL");
        if (client) {
            services = copy_services_with_strategy("createWithType", client, false, true);
            if (!services) {
                services = copy_services_with_strategy("createWithType+match", client, true, true);
            }
        }
    } else {
        printf("  calling IOHIDEventSystemClientCreateSimpleClient()\n");
        client = IOHIDEventSystemClientCreateSimpleClient(kCFAllocatorDefault);
        printf("  createSimpleClient returned: %s\n", client ? "non-NULL" : "NULL");
        if (client) {
            services = copy_services_with_strategy("simple", client, false, false);
        }

        if (!services && client) {
            CFRelease(client);
            client = NULL;
        }

        if (!services) {
            printf("  calling IOHIDEventSystemClientCreate()\n");
            client = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
            printf("  create returned: %s\n", client ? "non-NULL" : "NULL");
            if (client) {
                services = copy_services_with_strategy("create", client, false, true);
                if (!services) {
                    services = copy_services_with_strategy("create+match", client, true, true);
                }
            }
        }
    }

    if (!client) {
        fprintf(stderr, "  ERROR: all client creation attempts returned NULL\n");
        return 1;
    }
    if (!services) {
        fprintf(stderr, "  ERROR: no strategy produced a visible HID event service list\n");
        CFRelease(client);
        return 1;
    }

    CFIndex count = CFArrayGetCount(services);
    printf("  Total HID event services visible: %ld\n\n", (long)count);

    int matched_services = 0;
    int successful_known_reads = 0;
    int sweep_hits = 0;

    for (CFIndex i = 0; i < count; i++) {
        IOHIDServiceClientRef service = (IOHIDServiceClientRef)CFArrayGetValueAtIndex(services, i);
        if (!service) continue;

        int usage_page = copy_service_int(service, CFSTR(kIOHIDPrimaryUsagePageKey), -1);
        int usage = copy_service_int(service, CFSTR(kIOHIDPrimaryUsageKey), -1);
        char *transport = copy_service_string(service, CFSTR(kIOHIDTransportKey));
        char *product = copy_service_string(service, CFSTR(kIOHIDProductKey));
        bool motion_restricted = copy_service_bool(service, CFSTR("motionRestrictedService"), false);

        bool is_spu = transport && strcmp(transport, "SPU") == 0;
        const SensorTarget *target = target_for_usage(usage);
        if (!is_spu || !target || usage_page != 0xFF00) {
            free(transport);
            free(product);
            continue;
        }

        matched_services++;
        printf("  [%ld] target=%s usage_page=0x%04x usage=%d transport=%s product=%s motionRestricted=%s\n",
               (long)i,
               target->label,
               usage_page,
               usage,
               transport ? transport : "(null)",
               product ? product : "(null)",
               motion_restricted ? "yes" : "no");

        set_sensor_intervals(service);
        if (try_known_event(service, target, 6)) {
            successful_known_reads++;
        }
        sweep_hits += sweep_event_types(service, 0, 31);
        printf("\n");

        free(transport);
        free(product);
    }

    CFRelease(services);
    CFRelease(client);

    printf("═══════════════════════════════════════════════════════\n");
    printf("  SUMMARY\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  matched SPU motion services: %d\n", matched_services);
    printf("  known accel/gyro event reads: %d\n", successful_known_reads);
    printf("  generic sweep hits: %d\n", sweep_hits);
    if (successful_known_reads > 0 || sweep_hits > 0) {
        printf("  ★ Event-system path produced motion-related events.\n");
    } else {
        printf("  ✗ Event-system path produced no readable events in this probe.\n");
    }
    printf("═══════════════════════════════════════════════════════\n");

    return 0;
}
