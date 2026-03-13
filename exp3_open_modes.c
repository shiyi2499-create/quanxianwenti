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
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct {
    const char *mode_name;
    IOOptionBits options;
    IOReturn result;
} OpenTest;

int main(void) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  EXP-3: IOHIDDeviceOpen Mode Comparison\n");
    printf("  euid: %d | is_root: %s\n", geteuid(), geteuid() == 0 ? "yes" : "no");
    printf("═══════════════════════════════════════════════════════\n\n");

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, 0);
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int32_t page = 0xFF00;
    CFNumberRef pn = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &page);
    CFDictionarySetValue(match, CFSTR(kIOHIDDeviceUsagePageKey), pn);
    IOHIDManagerSetDeviceMatching(mgr, match);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);

    CFSetRef devs = IOHIDManagerCopyDevices(mgr);
    if (!devs || CFSetGetCount(devs) == 0) {
        printf("  No devices on 0xFF00. Aborting.\n");
        return 1;
    }

    /* Test each usage (3=accel, 9=gyro) × each open mode */
    int usages[] = {3, 9};
    const char *usage_names[] = {"accel", "gyro"};
    OpenTest modes[] = {
        {"kIOHIDOptionsTypeNone",         kIOHIDOptionsTypeNone,        0},
        {"kIOHIDOptionsTypeSeizeDevice",  kIOHIDOptionsTypeSeizeDevice, 0},
    };

    CFIndex count = CFSetGetCount(devs);
    const void **vals = calloc(count, sizeof(void*));
    CFSetGetValues(devs, vals);

    printf("  %-8s  %-35s  %-12s  %s\n", "Sensor", "OpenMode", "IOReturn", "Result");
    printf("  %-8s  %-35s  %-12s  %s\n", "------", "--------", "--------", "------");

    for (int ui = 0; ui < 2; ui++) {
        /* Find device with this usage */
        IOHIDDeviceRef target = NULL;
        for (CFIndex i = 0; i < count; i++) {
            IOHIDDeviceRef d = (IOHIDDeviceRef)vals[i];
            CFNumberRef uref = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDDeviceUsageKey));
            CFNumberRef upref = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDDeviceUsagePageKey));
            int32_t u = 0, up = 0;
            if (uref) CFNumberGetValue(uref, kCFNumberSInt32Type, &u);
            if (upref) CFNumberGetValue(upref, kCFNumberSInt32Type, &up);
            if (up == 0xFF00 && u == usages[ui]) { target = d; break; }
        }
        if (!target) {
            printf("  %-8s  %-35s  %-12s  %s\n",
                   usage_names[ui], "N/A", "N/A", "NOT_FOUND");
            continue;
        }

        for (int mi = 0; mi < 2; mi++) {
            IOReturn ret = IOHIDDeviceOpen(target, modes[mi].options);
            modes[mi].result = ret;

            const char *status;
            if (ret == kIOReturnSuccess) {
                status = "✓ SUCCESS";
                IOHIDDeviceClose(target, 0);
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

    free(vals);
    CFRelease(devs);
    IOHIDManagerClose(mgr, 0);
    CFRelease(match);
    CFRelease(pn);
    return 0;
}
