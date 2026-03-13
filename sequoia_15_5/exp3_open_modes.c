/*
 * EXP-3: IOHIDDeviceOpen Mode Comparison
 * ========================================
 * 测试两种打开模式在不同 euid 下的行为差异：
 *   Mode A: kIOHIDOptionsTypeNone        (0x00) — 非独占
 *   Mode B: kIOHIDOptionsTypeSeizeDevice  (0x01) — 独占（需要 root on keyboard devices）
 *
 * 根据 Apple TN2187:
 *   "kIOHIDOptionsTypeSeizeDevice requires root privileges for keyboard devices"
 * SPU IMU 不是 keyboard (usage page 0xFF00 != 0x01)，所以理论上 SeizeDevice
 * 也可能不需要 root。本实验验证此假设。
 *
 * 编译:
 *   clang -o exp3_open_modes exp3_open_modes.c \
 *       -framework IOKit -framework CoreFoundation -Wall -O2
 *
 * 运行:
 *   ./exp3_open_modes           # non-root
 *   sudo ./exp3_open_modes      # root
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct {
    const char *mode_name;
    IOOptionBits options;
    IOReturn result;
} OpenTest;

typedef struct {
    io_service_t service;
    IOHIDDeviceRef device;
    int usage_page;
    int usage;
    char transport[128];
} ServiceMatch;

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
    if (IOServiceGetMatchingServices(
            kIOMainPortDefault,
            IOServiceMatching("AppleSPUHIDDriver"),
            &iter) != KERN_SUCCESS) {
        return;
    }

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

static ServiceMatch find_spu_device_for_usage(int usage) {
    ServiceMatch match = {0};
    io_iterator_t iter = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(
            kIOMainPortDefault,
            IOServiceMatching("AppleSPUHIDDevice"),
            &iter) != KERN_SUCCESS) {
        return match;
    }

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int up = get_registry_int_property(service, CFSTR("PrimaryUsagePage"));
        int u = get_registry_int_property(service, CFSTR("PrimaryUsage"));
        if (up == 0xFF00 && u == usage) {
            match.service = service;
            match.device = IOHIDDeviceCreate(kCFAllocatorDefault, service);
            match.usage_page = up;
            match.usage = u;
            get_registry_string_property(service, CFSTR("Transport"),
                                         match.transport, sizeof(match.transport));
            break;
        }
        IOObjectRelease(service);
    }

    IOObjectRelease(iter);
    return match;
}

int main(void) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  EXP-3: IOHIDDeviceOpen Mode Comparison\n");
    printf("  euid: %d | is_root: %s\n", geteuid(), geteuid() == 0 ? "yes" : "no");
    print_macos_metadata();
    printf("═══════════════════════════════════════════════════════\n\n");
    wake_spu_drivers();

    /* Test each usage (3=accel, 9=gyro) × each open mode */
    int usages[] = {3, 9};
    const char *usage_names[] = {"accel", "gyro"};
    OpenTest modes[] = {
        {"kIOHIDOptionsTypeNone",         kIOHIDOptionsTypeNone,        0},
        {"kIOHIDOptionsTypeSeizeDevice",  kIOHIDOptionsTypeSeizeDevice, 0},
    };

    printf("  %-8s  %-35s  %-12s  %s\n", "Sensor", "OpenMode", "IOReturn", "Result");
    printf("  %-8s  %-35s  %-12s  %s\n", "------", "--------", "--------", "------");

    for (int ui = 0; ui < 2; ui++) {
        ServiceMatch match = find_spu_device_for_usage(usages[ui]);
        if (!match.device) {
            printf("  %-8s  %-35s  %-12s  %s\n",
                   usage_names[ui], "N/A", "N/A", "NOT_FOUND");
            continue;
        }

        printf("  %-8s  %-35s  %-12s  transport=%s\n",
               usage_names[ui], "SERVICE_MATCH", "page=0xff00",
               match.transport[0] ? match.transport : "(null)");

        for (int mi = 0; mi < 2; mi++) {
            IOReturn ret = IOHIDDeviceOpen(match.device, modes[mi].options);
            modes[mi].result = ret;

            const char *status;
            if (ret == kIOReturnSuccess) {
                status = "✓ SUCCESS";
                IOHIDDeviceClose(match.device, 0);
            } else if (ret == (IOReturn)0xe00002e2) {
                status = "✗ NOT_PERMITTED";
            } else if (ret == (IOReturn)0xe00002c5) {
                status = "✗ EXCLUSIVE_ACCESS";
            } else {
                status = "✗ OTHER_ERROR";
            }

            printf("  %-8s  %-35s  0x%08x  %s\n",
                   usage_names[ui], modes[mi].mode_name, ret, status);
        }

        CFRelease(match.device);
        IOObjectRelease(match.service);
    }

    printf("\n  ─── INTERPRETATION ───\n");
    printf("  If None=SUCCESS + Seize=FAIL (non-root):\n");
    printf("    → Non-exclusive read works; only SeizeDevice needs root.\n");
    printf("    → Attack is viable without root.\n");
    printf("  If both FAIL (non-root):\n");
    printf("    → AppleSPUHIDDriver enforces root at kernel level.\n");
    printf("    → Root or entitlement required.\n");
    printf("  If both SUCCESS (non-root):\n");
    printf("    → Full unrestricted access. High severity finding.\n");
    return 0;
}
