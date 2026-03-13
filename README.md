# Apple Internal IMU Permission Audit on macOS

This repository contains a focused permission audit for Apple internal SPU IMU access on Apple Silicon Macs. The goal is not model training or paper evaluation yet. The goal is to answer one question first:

Can a non-root process discover, open, and read the Apple internal IMU needed for Stage 2 data collection?

## Scope

This repo covers the first-layer permission audit only:

- `EXP-0`: device discovery
- `EXP-1`: `macimu` library root-gate bypass behavior
- `EXP-2`: native C IOKit IMU PoC
- `EXP-3`: open-mode comparison (`None` vs `SeizeDevice`)
- `EXP-4`: TCC / Input Monitoring inspection
- `EXP-5`: background persistence test scaffold

It does not yet cover keystroke classification, free-type decoding, or paper-level model experiments.

## Final Tahoe Conclusion

On the tested machine below, a non-root process can read the Apple internal IMU without root privileges.

More precisely:

- On `macOS 26 Tahoe`, the `IOHIDManager` path is affected by `Input Monitoring`.
- However, the direct `IOServiceMatching("AppleSPUHIDDevice")` plus `IOHIDDeviceOpen(...)` path still succeeds even when `Input Monitoring` is disabled.
- Therefore, on Tahoe, `Input Monitoring` is not a necessary condition for direct non-root IMU reads.
- This is sufficient for Stage 2 raw IMU data collection, because Stage 2 needs accelerometer and gyroscope samples, not root privileges per se.

## Tested Environment

- Machine: `Apple M4`
- Model: `Mac16,12`
- macOS: `26.3`
- Build: `25D125`
- User context: `euid=501`
- SIP: `enabled`

## What Was Fixed During The Audit

The original experiments were too conservative in two ways:

1. They relied too heavily on `IOHIDManagerOpen(...)` and a manager-based matching path.
2. They did not robustly handle Tahoe-visible SPU properties such as `PrimaryUsagePage` / `PrimaryUsage`.

The following files were updated to remove those blind spots:

- [exp0_device_discovery.py](exp0_device_discovery.py)
- [exp2_iokit_imu.c](exp2_iokit_imu.c)
- [exp3_open_modes.c](exp3_open_modes.c)
- [exp4_tcc_check.py](exp4_tcc_check.py)

Main improvements:

- Added service-level `AppleSPUHIDDevice` enumeration instead of trusting only HID manager device properties.
- Added fallback handling for `PrimaryUsagePage` / `PrimaryUsage` vs older property names.
- Switched the C PoC to a direct service path so that a failed `IOHIDManagerOpen(...)` no longer incorrectly implies "cannot read IMU".
- Added macOS version/build metadata to logs so Tahoe and Sequoia results can be compared cleanly.
- Improved host-app detection in the TCC probe so `Codex` / `Python.app` / shell chain are visible in the report.

## B Group: Input Monitoring Enabled

This was the "permission on" group.

Evidence directory on the local machine:

- `results/ab_group_B_20260313_223208/`

These raw logs are intentionally kept in the local `results/` directory and are gitignored by default.

Key results:

1. `EXP-0` successfully opened the HID manager and discovered SPU IMU devices.
   - File: `results/ab_group_B_20260313_223208/exp0_B.log`
   - Result: `usage 3` accelerometer and `usage 9` gyroscope were both discovered.

2. `EXP-2` successfully read real IMU data as non-root.
   - File: `results/ab_group_B_20260313_223208/exp2_B.log`
   - Result:
     - `IOHIDManagerOpen(None) = 0x00000000`
     - `IOHIDDeviceOpen(accel, None) = SUCCESS`
     - `IOHIDDeviceOpen(gyro, None) = SUCCESS`
     - `accel callbacks: 100`
     - `gyro callbacks: 100`

3. `EXP-3` showed both open modes succeed as non-root.
   - File: `results/ab_group_B_20260313_223208/exp3_B.log`
   - Result:
     - `kIOHIDOptionsTypeNone = SUCCESS`
     - `kIOHIDOptionsTypeSeizeDevice = SUCCESS`

Interpretation:

- With Input Monitoring enabled, both the manager path and the direct service path work on Tahoe.

## A Group: Input Monitoring Disabled

This was the "permission off" group after disabling Input Monitoring and restarting the host app.

Evidence directory on the local machine:

- `results/ab_group_A_20260313_225558/`

These raw logs are intentionally kept in the local `results/` directory and are gitignored by default.

Key results:

1. `EXP-0` failed at the HID manager layer.
   - File: `results/ab_group_A_20260313_225558/exp0_A.log`
   - Result:
     - `IOHIDManagerOpen(None) = 0xe00002e2`

2. The first version of `EXP-2` also failed for the same reason because it still treated manager failure as a stop condition.
   - File: `results/ab_group_A_20260313_225558/exp2_A.log`
   - Result:
     - `IOHIDManagerOpen(None) = 0xe00002e2`

3. `EXP-3` still succeeded because it used direct service matching.
   - File: `results/ab_group_A_20260313_225558/exp3_A.log`
   - Result:
     - `IOHIDDeviceOpen(accel, None) = SUCCESS`
     - `IOHIDDeviceOpen(gyro, None) = SUCCESS`
     - `IOHIDDeviceOpen(..., SeizeDevice) = SUCCESS`

4. After updating `EXP-2` to continue through the direct service path even if manager open fails, non-root IMU reads still succeeded with Input Monitoring disabled.
   - File: `results/ab_group_A_20260313_225558/exp2_A_direct_service.log`
   - Result:
     - `IOHIDManagerOpen(None) = 0xe00002e2`
     - `Accelerometer (usage 3): FOUND`
     - `Gyroscope (usage 9): FOUND`
     - `IOHIDDeviceOpen(accel, None) = SUCCESS`
     - `IOHIDDeviceOpen(gyro, None) = SUCCESS`
     - `accel callbacks: 100`
     - `gyro callbacks: 100`

Interpretation:

- On Tahoe, `Input Monitoring` blocks or perturbs the `IOHIDManager` route.
- It does not block the direct `AppleSPUHIDDevice` service route.
- Therefore the direct route is the stronger attack path and the one that matters for Stage 2.

## A/B Result Summary

| Path | Input Monitoring ON | Input Monitoring OFF | Conclusion |
|------|---------------------|----------------------|------------|
| `IOHIDManagerOpen(...)` | Success | `0xe00002e2` failure | Manager path is permission-sensitive |
| Direct `AppleSPUHIDDevice` match | Success | Success | Direct service path survives |
| `IOHIDDeviceOpen(accel/gyro, None)` | Success | Success | Non-root open works either way |
| Real accel/gyro callbacks | Success | Success | Stage 2 raw data remains collectible |

## Stage 2 Implication

Yes, the current audit now supports the following statement:

On the tested Tahoe machine, Stage 2 IMU data collection does not require root, and it does not require Input Monitoring if the collector uses the direct `AppleSPUHIDDevice` IOKit service path.

This statement is about the OS-level data path. It is not automatically a statement that every existing Stage 2 collector script already works unchanged, because library-specific issues may still exist.

For example:

- `macimu` originally enforced a library-level root gate.
- The original `macimu` shared-memory naming path introduced side effects during testing.

Those are implementation issues, not proof that the OS requires root.

## TCC Status

`EXP-4` improves host-app detection, but TCC is still not fully closed as a database-level claim because the current app context does not have sufficient access to read `TCC.db`.

Evidence:

- `results/ab_group_B_20260313_223208/exp4_B.log`
- `results/ab_group_A_20260313_225558/exp4_A.log`

Current safe wording:

- We can prove that `Input Monitoring` changes `IOHIDManager` behavior on Tahoe.
- We cannot yet claim from `TCC.db` records alone exactly how Apple wires the policy internally.

## How To Reproduce On Another Machine

Use the same updated scripts on another host, especially `macOS 15.5 Sequoia`, and compare:

1. Run `python3 exp0_device_discovery.py`
2. Compile and run `./exp2_iokit_imu`
3. Run `./exp3_open_modes`
4. Run `python3 exp4_tcc_check.py`
5. Run once with Input Monitoring enabled and once with it disabled

The most important cross-version question is:

Does Sequoia also allow direct non-root IMU reads through `AppleSPUHIDDevice`, or is this a Tahoe-specific regression / policy change?

## Files In This Repository

- [exp0_device_discovery.py](exp0_device_discovery.py): HID manager discovery plus service-level SPU enumeration
- [exp1_macimu_patch_test.py](exp1_macimu_patch_test.py): `macimu` root-gate bypass experiment
- [exp2_iokit_imu.c](exp2_iokit_imu.c): native C IMU read PoC
- [exp3_open_modes.c](exp3_open_modes.c): open-mode comparison
- [exp4_tcc_check.py](exp4_tcc_check.py): TCC and host-app visibility probe
- [exp5_background_persistence.py](exp5_background_persistence.py): persistence scaffold for future root/control testing
- [run_all_experiments.sh](run_all_experiments.sh): convenience runner

## Current Bottom Line

For `macOS 26 Tahoe` on the tested M4 machine:

- non-root IMU access is real
- root is not required
- `Input Monitoring` is not required for the direct service path
- the direct service path is enough for Stage 2 raw IMU collection

The next critical validation target is `macOS 15.5 Sequoia`.
