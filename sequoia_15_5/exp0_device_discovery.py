#!/usr/bin/env python3
"""
EXP-0: IOKit Device Discovery (Zero Privilege)
================================================
验证 AppleSPUHIDDevice 是否可被非 root 用户发现（enumerate，不 open）。
这一步不需要任何权限——如果连 enumerate 都失败，说明整个 IOKit 路径不可用。

判定标准:
  SUCCESS: 输出包含 "FOUND AppleSPUHIDDevice" 且 usage_page=0xff00
  FAILURE: "NO SPU DEVICE FOUND"
"""

import os
import json
import ctypes
import ctypes.util
import subprocess
from datetime import datetime

# ── Load frameworks via ctypes ──────────────────────────────────
cf = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))
iokit = ctypes.cdll.LoadLibrary(ctypes.util.find_library("IOKit"))

# CoreFoundation types
CFTypeRef = ctypes.c_void_p
CFStringRef = ctypes.c_void_p
CFNumberRef = ctypes.c_void_p
CFDictionaryRef = ctypes.c_void_p
CFMutableDictionaryRef = ctypes.c_void_p
CFAllocatorRef = ctypes.c_void_p
IOHIDManagerRef = ctypes.c_void_p

kCFAllocatorDefault = None
kCFNumberIntType = 9  # kCFNumberSInt32Type

# ── CoreFoundation helpers ──────────────────────────────────────
cf.CFStringCreateWithCString.restype = CFStringRef
cf.CFStringCreateWithCString.argtypes = [CFAllocatorRef, ctypes.c_char_p, ctypes.c_uint32]

cf.CFNumberCreate.restype = CFNumberRef
cf.CFNumberCreate.argtypes = [CFAllocatorRef, ctypes.c_int, ctypes.c_void_p]

cf.CFDictionaryCreateMutable.restype = CFMutableDictionaryRef
cf.CFDictionaryCreateMutable.argtypes = [
    CFAllocatorRef, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p
]

cf.CFDictionarySetValue.restype = None
cf.CFDictionarySetValue.argtypes = [CFMutableDictionaryRef, ctypes.c_void_p, ctypes.c_void_p]

cf.CFRelease.restype = None
cf.CFRelease.argtypes = [CFTypeRef]

cf.CFSetGetCount.restype = ctypes.c_long
cf.CFSetGetCount.argtypes = [ctypes.c_void_p]

cf.CFSetGetValues.restype = None
cf.CFSetGetValues.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]

# IOKit HID Manager
iokit.IOHIDManagerCreate.restype = IOHIDManagerRef
iokit.IOHIDManagerCreate.argtypes = [CFAllocatorRef, ctypes.c_uint32]

iokit.IOHIDManagerSetDeviceMatching.restype = None
iokit.IOHIDManagerSetDeviceMatching.argtypes = [IOHIDManagerRef, CFDictionaryRef]

iokit.IOHIDManagerCopyDevices.restype = ctypes.c_void_p
iokit.IOHIDManagerCopyDevices.argtypes = [IOHIDManagerRef]

iokit.IOHIDManagerOpen.restype = ctypes.c_int
iokit.IOHIDManagerOpen.argtypes = [IOHIDManagerRef, ctypes.c_uint32]

iokit.IOHIDManagerClose.restype = ctypes.c_int
iokit.IOHIDManagerClose.argtypes = [IOHIDManagerRef, ctypes.c_uint32]

# IOHIDDevice property
iokit.IOHIDDeviceGetProperty.restype = CFTypeRef
iokit.IOHIDDeviceGetProperty.argtypes = [ctypes.c_void_p, CFStringRef]
iokit.IOServiceMatching.restype = ctypes.c_void_p
iokit.IOServiceMatching.argtypes = [ctypes.c_char_p]
iokit.IOServiceGetMatchingServices.restype = ctypes.c_int
iokit.IOServiceGetMatchingServices.argtypes = [
    ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)
]
iokit.IOIteratorNext.restype = ctypes.c_uint
iokit.IOIteratorNext.argtypes = [ctypes.c_uint]
iokit.IOObjectRelease.restype = ctypes.c_int
iokit.IOObjectRelease.argtypes = [ctypes.c_uint]
iokit.IORegistryEntryCreateCFProperty.restype = ctypes.c_void_p
iokit.IORegistryEntryCreateCFProperty.argtypes = [
    ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint
]

cf.CFNumberGetValue.restype = ctypes.c_bool
cf.CFNumberGetValue.argtypes = [CFNumberRef, ctypes.c_int, ctypes.c_void_p]

cf.CFStringGetCString.restype = ctypes.c_bool
cf.CFStringGetCString.argtypes = [CFStringRef, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
cf.CFGetTypeID.restype = ctypes.c_ulong
cf.CFGetTypeID.argtypes = [ctypes.c_void_p]
cf.CFStringGetTypeID.restype = ctypes.c_ulong
cf.CFStringGetTypeID.argtypes = []
cf.CFNumberGetTypeID.restype = ctypes.c_ulong
cf.CFNumberGetTypeID.argtypes = []

CF_UTF8 = 0x08000100
CF_SINT64 = 4


def cfstr(s: str) -> CFStringRef:
    return cf.CFStringCreateWithCString(kCFAllocatorDefault, s.encode(), CF_UTF8)


def cfnum(val: int) -> CFNumberRef:
    v = ctypes.c_int32(val)
    return cf.CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, ctypes.byref(v))


def get_device_int_property(device, key_str):
    key = cfstr(key_str)
    prop = iokit.IOHIDDeviceGetProperty(device, key)
    cf.CFRelease(key)
    if not prop:
        return None
    val = ctypes.c_int32()
    if cf.CFNumberGetValue(prop, kCFNumberIntType, ctypes.byref(val)):
        return val.value
    return None


def get_device_str_property(device, key_str):
    key = cfstr(key_str)
    prop = iokit.IOHIDDeviceGetProperty(device, key)
    cf.CFRelease(key)
    if not prop:
        return None
    buf = ctypes.create_string_buffer(256)
    if cf.CFStringGetCString(prop, buf, 256, CF_UTF8):
        return buf.value.decode("utf-8", errors="replace")
    return None


def get_first_int_property(device, keys):
    for key in keys:
        value = get_device_int_property(device, key)
        if value is not None:
            return value, key
    return None, None


def get_first_str_property(device, keys):
    for key in keys:
        value = get_device_str_property(device, key)
        if value:
            return value, key
    return None, None


def get_registry_property(service, key_str):
    key = cfstr(key_str)
    ref = iokit.IORegistryEntryCreateCFProperty(service, key, None, 0)
    cf.CFRelease(key)
    if not ref:
        return None

    try:
        type_id = cf.CFGetTypeID(ref)
        if type_id == cf.CFStringGetTypeID():
            buf = ctypes.create_string_buffer(256)
            if cf.CFStringGetCString(ref, buf, 256, CF_UTF8):
                return buf.value.decode("utf-8", errors="replace")
            return None
        if type_id == cf.CFNumberGetTypeID():
            value = ctypes.c_long()
            if cf.CFNumberGetValue(ref, CF_SINT64, ctypes.byref(value)):
                return value.value
            return None
        return None
    finally:
        cf.CFRelease(ref)


def enumerate_spu_services():
    matching = iokit.IOServiceMatching(b"AppleSPUHIDDevice")
    iterator = ctypes.c_uint()
    kr = iokit.IOServiceGetMatchingServices(0, matching, ctypes.byref(iterator))
    if kr != 0:
        return []

    services = []
    while True:
        service = iokit.IOIteratorNext(iterator.value)
        if not service:
            break
        info = {
            "primary_usage_page": get_registry_property(service, "PrimaryUsagePage"),
            "primary_usage": get_registry_property(service, "PrimaryUsage"),
            "product": get_registry_property(service, "Product"),
            "transport": get_registry_property(service, "Transport"),
            "vendor_id": get_registry_property(service, "VendorID"),
            "product_id": get_registry_property(service, "ProductID"),
            "manufacturer": get_registry_property(service, "Manufacturer"),
            "serial_number": get_registry_property(service, "SerialNumber"),
        }
        services.append(info)
        iokit.IOObjectRelease(service)

    iokit.IOObjectRelease(iterator.value)
    return services


def get_system_metadata():
    def run_cmd(args):
        try:
            return subprocess.check_output(args, stderr=subprocess.DEVNULL).decode().strip()
        except Exception:
            return None

    return {
        "macos_version": run_cmd(["sw_vers", "-productVersion"]),
        "macos_build": run_cmd(["sw_vers", "-buildVersion"]),
    }


def main():
    print(f"  EXP-0: IOKit Device Discovery")
    print(f"  euid: {os.geteuid()}")
    print(f"  is_root: {os.geteuid() == 0}")
    print()

    results = {
        "experiment": "EXP-0",
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "euid": os.geteuid(),
        "is_root": os.geteuid() == 0,
        "system": get_system_metadata(),
        "manager_open_result": None,
        "devices_found": [],
        "service_devices_found": [],
        "spu_accel_found": False,
        "spu_gyro_found": False,
        "verdict": "PENDING",
    }

    # Create HID manager (does not open devices)
    manager = iokit.IOHIDManagerCreate(kCFAllocatorDefault, 0)
    if not manager:
        print("  FAIL: Could not create IOHIDManager")
        results["verdict"] = "FAIL_MANAGER_CREATE"
        print(json.dumps(results, indent=2))
        return

    # Match vendor usage page 0xFF00 (where SPU sensors live)
    kCFTypeDictionaryKeyCallBacks = ctypes.c_void_p.in_dll(cf, "kCFTypeDictionaryKeyCallBacks")
    kCFTypeDictionaryValueCallBacks = ctypes.c_void_p.in_dll(cf, "kCFTypeDictionaryValueCallBacks")

    match_dict = cf.CFDictionaryCreateMutable(
        kCFAllocatorDefault, 2,
        ctypes.byref(kCFTypeDictionaryKeyCallBacks),
        ctypes.byref(kCFTypeDictionaryValueCallBacks),
    )
    page_key = cfstr("DeviceUsagePage")
    page_val = cfnum(0xFF00)
    cf.CFDictionarySetValue(match_dict, page_key, page_val)

    iokit.IOHIDManagerSetDeviceMatching(manager, match_dict)

    # Open manager with kIOHIDOptionsTypeNone (0) — enumerate only
    ret = iokit.IOHIDManagerOpen(manager, 0)
    results["manager_open_result"] = f"0x{ret & 0xFFFFFFFF:08x}"
    print(f"  IOHIDManagerOpen(kIOHIDOptionsTypeNone) returned: 0x{ret & 0xFFFFFFFF:08x}")
    device_set = None
    if ret != 0:
        print(f"  WARN: IOHIDManagerOpen returned error 0x{ret & 0xFFFFFFFF:08x}")
        print("  Continuing with direct AppleSPUHIDDevice service enumeration")
    else:
        # Get matched devices
        device_set = iokit.IOHIDManagerCopyDevices(manager)
        if not device_set:
            print("  NO DEVICES on usage page 0xFF00 via IOHIDManager")
        else:
            count = cf.CFSetGetCount(device_set)
            print(f"  Found {count} device(s) on usage page 0xFF00")

            devices = (ctypes.c_void_p * count)()
            cf.CFSetGetValues(device_set, devices)

            for i in range(count):
                dev = devices[i]
                usage_page, usage_page_key = get_first_int_property(
                    dev, ["PrimaryUsagePage", "DeviceUsagePage"]
                )
                usage, usage_key = get_first_int_property(dev, ["PrimaryUsage", "DeviceUsage"])
                product, product_key = get_first_str_property(dev, ["Product"])
                transport, transport_key = get_first_str_property(dev, ["Transport"])

                info = {
                    "index": i,
                    "usage_page": f"0x{usage_page:04x}" if usage_page is not None else None,
                    "usage": usage,
                    "product": product,
                    "transport": transport,
                    "usage_page_key": usage_page_key,
                    "usage_key": usage_key,
                    "product_key": product_key,
                    "transport_key": transport_key,
                }
                results["devices_found"].append(info)

                label = ""
                if usage_page == 0xFF00:
                    if usage == 3:
                        label = " ← ACCELEROMETER"
                        results["spu_accel_found"] = True
                    elif usage == 9:
                        label = " ← GYROSCOPE"
                        results["spu_gyro_found"] = True

                print(f"    [{i}] page={info['usage_page']} usage={usage} "
                      f"product='{product}' transport='{transport}'{label}")

    service_devices = enumerate_spu_services()
    results["service_devices_found"] = service_devices
    if service_devices:
        print("\n  Service-level AppleSPUHIDDevice enumeration:")
        for idx, service in enumerate(service_devices):
            page = service.get("primary_usage_page")
            usage = service.get("primary_usage")
            if page == 0xFF00 and usage == 3:
                results["spu_accel_found"] = True
            elif page == 0xFF00 and usage == 9:
                results["spu_gyro_found"] = True

            page_repr = f"0x{page:04x}" if isinstance(page, int) else None
            print(
                f"    [svc {idx}] page={page_repr} usage={usage} "
                f"product='{service.get('product')}' transport='{service.get('transport')}'"
            )

    if results["spu_accel_found"] or results["spu_gyro_found"]:
        results["verdict"] = "SUCCESS_SPU_DISCOVERED"
        print(f"\n  ✓ FOUND AppleSPUHIDDevice (accel={results['spu_accel_found']}, "
              f"gyro={results['spu_gyro_found']})")
    else:
        results["verdict"] = "NO_SPU_DEVICE_FOUND"
        print("\n  ✗ NO SPU DEVICE FOUND on 0xFF00")

    iokit.IOHIDManagerClose(manager, 0)
    if device_set:
        cf.CFRelease(device_set)
    cf.CFRelease(match_dict)
    cf.CFRelease(page_key)
    cf.CFRelease(page_val)

    print(f"\n  Result JSON:")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
