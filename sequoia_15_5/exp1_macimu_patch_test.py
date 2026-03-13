#!/usr/bin/env python3
"""
EXP-1: macimu Library Root Gate Bypass Test
============================================
假设 H1: macimu 的 root 检查只是库级别 gate，底层 IOKit 可能不需要 root。

方法: 在运行时 monkey-patch 掉 os.geteuid() 返回值（仅对 macimu 内部检查生效），
      然后尝试正常初始化 IMU 并读取数据。

这比直接改源码更安全——不修改任何文件，patch 仅在本进程生效。

判定标准:
  SUCCESS_NONROOT_READ:  非 root 下绕过 gate 后成功读到加速度数据
  FAIL_IOKIT_PERMISSION: gate 绕过了但 IOKit 层拒绝 → 确认是 OS 级限制
  FAIL_OTHER:            其他错误（库内部异常等）

用法:
  python3 exp1_macimu_patch_test.py              # 非 root 测试
  sudo python3 exp1_macimu_patch_test.py          # root 对照
"""

import os
import sys
import json
import time
import traceback
from datetime import datetime
from unittest.mock import patch

ACTUAL_EUID = os.geteuid()

results = {
    "experiment": "EXP-1",
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "euid": ACTUAL_EUID,
    "is_root": ACTUAL_EUID == 0,
    "macimu_installed": False,
    "patch_applied": False,
    "imu_available": None,
    "imu_init_success": False,
    "imu_read_success": False,
    "samples_read": 0,
    "error_type": None,
    "error_message": None,
    "iokit_return_code": None,
    "verdict": "PENDING",
}


def main():
    print(f"  EXP-1: macimu Root Gate Bypass Test")
    print(f"  euid: {ACTUAL_EUID}")
    print()

    # ── Step 1: Check macimu is installed ──────────────────────
    try:
        import macimu
        results["macimu_installed"] = True
        print(f"  macimu found: {macimu.__file__}")
    except ImportError:
        results["macimu_installed"] = False
        results["verdict"] = "SKIP_NOT_INSTALLED"
        print("  ✗ macimu not installed. Install with: pip install macimu")
        print(json.dumps(results, indent=2))
        return

    # ── Step 2: Check IMU.available() (no root needed) ────────
    try:
        from macimu import IMU
        avail = IMU.available()
        results["imu_available"] = avail
        print(f"  IMU.available(): {avail}")
    except Exception as e:
        results["imu_available"] = False
        results["error_message"] = str(e)
        print(f"  IMU.available() failed: {e}")

    # ── Step 3: Try reading WITHOUT patch (baseline) ──────────
    print(f"\n  --- Baseline (no patch, euid={ACTUAL_EUID}) ---")
    try:
        imu = IMU()
        imu.__enter__()
        samples = imu.read_accel()
        results["imu_init_success"] = True
        results["imu_read_success"] = True
        results["samples_read"] = len(list(samples))
        print(f"  ✓ Baseline read succeeded: {results['samples_read']} samples")
        imu.__exit__(None, None, None)
        results["verdict"] = "SUCCESS_BASELINE_WORKS"
        print(json.dumps(results, indent=2))
        return
    except PermissionError as e:
        print(f"  ✗ Baseline PermissionError: {e}")
        results["error_type"] = "PermissionError_baseline"
        results["error_message"] = str(e)
    except Exception as e:
        print(f"  ✗ Baseline error: {type(e).__name__}: {e}")
        results["error_type"] = f"{type(e).__name__}_baseline"
        results["error_message"] = str(e)

    if ACTUAL_EUID == 0:
        results["verdict"] = "FAIL_EVEN_AS_ROOT"
        print(json.dumps(results, indent=2))
        return

    # ── Step 4: Apply monkey-patch and retry ──────────────────
    print(f"\n  --- Patched (fake euid=0) ---")
    print(f"  Monkey-patching os.geteuid() to return 0...")
    results["patch_applied"] = True

    try:
        with patch("os.geteuid", return_value=0):
            # Also patch within macimu's imported os module if it cached it
            import macimu.core as _core  # noqa: might not exist
            if hasattr(_core, "os"):
                with patch.object(_core.os, "geteuid", return_value=0):
                    _try_read(results)
            else:
                _try_read(results)
    except ImportError:
        # macimu might not have a .core submodule; try direct patch
        with patch("os.geteuid", return_value=0):
            _try_read(results)
    except Exception as e:
        print(f"  ✗ Patch setup error: {type(e).__name__}: {e}")
        results["error_type"] = f"{type(e).__name__}_patch_setup"
        results["error_message"] = str(e)
        results["verdict"] = "FAIL_PATCH_SETUP"

    print(f"\n  Result JSON:")
    print(json.dumps(results, indent=2))


def _try_read(results):
    """Attempt IMU init + read under the active monkey-patch."""
    from macimu import IMU

    try:
        print(f"    os.geteuid() now returns: {os.geteuid()}")
        imu = IMU()
        imu.__enter__()
        results["imu_init_success"] = True
        print(f"    ✓ IMU.__enter__() succeeded (gate bypassed)")
    except PermissionError as e:
        # The library might check root in __enter__ or start()
        results["imu_init_success"] = False
        results["error_type"] = "PermissionError_init"
        results["error_message"] = str(e)
        results["verdict"] = "FAIL_GATE_NOT_ONLY_CHECK"
        print(f"    ✗ Still got PermissionError in init: {e}")
        print(f"    → There may be additional root checks beyond geteuid()")
        return
    except OSError as e:
        results["imu_init_success"] = False
        # Look for IOKit return codes in the error message
        err_str = str(e)
        results["error_type"] = "OSError_init"
        results["error_message"] = err_str
        if "not permitted" in err_str.lower() or "e00002e2" in err_str.lower():
            results["verdict"] = "FAIL_IOKIT_PERMISSION"
            results["iokit_return_code"] = "kIOReturnNotPermitted (0xe00002e2)"
            print(f"    ✗ IOKit returned kIOReturnNotPermitted")
            print(f"    → OS-level restriction confirmed (not just library gate)")
        else:
            results["verdict"] = "FAIL_IOKIT_OTHER"
            print(f"    ✗ IOKit error: {e}")
        return
    except Exception as e:
        results["imu_init_success"] = False
        results["error_type"] = f"{type(e).__name__}_init"
        results["error_message"] = str(e)
        results["verdict"] = "FAIL_INIT_OTHER"
        print(f"    ✗ Init error: {type(e).__name__}: {e}")
        traceback.print_exc()
        return

    # Try reading data
    try:
        time.sleep(0.5)  # Let sensor warm up
        samples = list(imu.read_accel())
        results["imu_read_success"] = True
        results["samples_read"] = len(samples)
        if samples:
            s = samples[0]
            print(f"    ✓ Read {len(samples)} accel samples!")
            print(f"    First sample: x={s.x:.4f}g y={s.y:.4f}g z={s.z:.4f}g")
            results["verdict"] = "SUCCESS_NONROOT_READ"
        else:
            results["verdict"] = "PARTIAL_NO_DATA"
            print(f"    ⚠ IMU opened but read returned 0 samples")
    except Exception as e:
        results["imu_read_success"] = False
        results["error_type"] = f"{type(e).__name__}_read"
        results["error_message"] = str(e)
        results["verdict"] = "FAIL_READ"
        print(f"    ✗ Read error: {type(e).__name__}: {e}")

    try:
        imu.__exit__(None, None, None)
    except:
        pass


if __name__ == "__main__":
    main()
