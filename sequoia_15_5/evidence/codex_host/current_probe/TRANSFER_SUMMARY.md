# Sequoia IMU Audit Transfer Summary

Timestamp: 2026-03-13 23:40 Asia/Shanghai

## Scope

This record captures the current Sequoia first-layer IMU permission audit status on the local machine. It does not include any Stage 2 model experiment.

Current status:

- README has been read in full.
- Only first-layer audit work has been performed.
- A current non-root probe has been completed and archived under `current_probe/`.
- A claimed B-group run with Input Monitoring enabled has also been completed under `B_group_enabled_20260314_000033/`.

## Local Environment

- macOS version: `15.5`
- macOS build: `24F74`
- Chip: `Apple M4`
- Model: `Mac16,12`
- Architecture: `arm64`
- euid: `501`
- SIP: `enabled`

Raw environment files:

- `current_probe/env_sw_vers.txt`
- `current_probe/env_hardware.txt`
- `current_probe/env_arch.txt`
- `current_probe/env_euid.txt`
- `current_probe/env_sip.txt`

## Minimal Compatibility Fix Applied

File changed:

- `exp0_device_discovery.py`

Reason:

- The original `EXP-0` stopped immediately when `IOHIDManagerOpen(...)` failed.
- That behavior is too conservative for Sequoia/Tahoe comparison because it suppresses direct `AppleSPUHIDDevice` service enumeration.
- The file was minimally updated so `EXP-0` now records the manager open result and continues with service-level enumeration even if the manager path fails.

This change is intended only to preserve the stronger direct service signal required by the audit.

## Experiments Completed In This Round

The following experiments were run as non-root in the current host-app state:

- `EXP-0`
- `EXP-2`
- `EXP-3`
- `EXP-4`

Raw logs:

- `current_probe/exp0_current.log`
- `current_probe/exp2_current.log`
- `current_probe/exp3_current.log`
- `current_probe/exp4_current.log`

Additional B-group logs:

- `B_group_enabled_20260314_000033/exp0_B.log`
- `B_group_enabled_20260314_000033/exp2_B.log`
- `B_group_enabled_20260314_000033/exp3_B.log`
- `B_group_enabled_20260314_000033/exp4_B.log`
- `B_group_enabled_20260314_000033/SUMMARY.md`

Additional registry evidence:

- `current_probe/ioreg_AppleSPUHIDDevice.txt`
- `current_probe/ioreg_AppleSPUHIDDriver.txt`

## Current Probe Results

### EXP-0

- `IOHIDManagerOpen(kIOHIDOptionsTypeNone) = 0xe00002e2`
- Direct `AppleSPUHIDDevice` service enumeration still succeeds.
- SPU accelerometer was found at usage `3`.
- SPU gyroscope was found at usage `9`.

Interpretation:

- Sequoia still exposes the SPU IMU in IORegistry.
- Manager path failure does not imply the service path is absent.

### EXP-2

- `IOHIDManagerOpen(None) = 0xe00002e2`
- Direct service match found both accel and gyro.
- `IOHIDDeviceOpen(accel, None) = 0xe00002e2`
- `IOHIDDeviceOpen(gyro, None) = 0xe00002e2`
- No samples were read.

Interpretation:

- The failure is happening at device open time, not at service discovery time.
- This is not a simple "device not found" failure mode.

### EXP-3

- Direct service match succeeded for accel and gyro.
- `kIOHIDOptionsTypeNone` failed with `0xe00002e2`
- `kIOHIDOptionsTypeSeizeDevice` failed with `0xe00002e2`

Interpretation:

- On the current Sequoia probe, both non-exclusive and seize paths are denied for non-root.

### EXP-4

- `TCC.db` could not be read from the current app context.
- The host app was not positively identified by the script in this run.
- This run therefore cannot yet prove TCC state from database entries alone.

Interpretation:

- The current run looks like an unprivileged or not-confirmed Input Monitoring state.
- TCC still needs the explicit enabled-group re-run for a clean A/B comparison.

## Key Cross-Version Observation Against README Tahoe

README Tahoe says:

- `IOHIDManager` path is permission-sensitive.
- Direct `AppleSPUHIDDevice` path still succeeds without Input Monitoring.

Current Sequoia probe says:

- `IOHIDManager` path is permission-sensitive here too.
- Direct `AppleSPUHIDDevice` enumeration succeeds.
- But direct `IOHIDDeviceOpen(...)` does not currently succeed as non-root.

Practical meaning:

- Sequoia currently does not reproduce the Tahoe direct-read result in this host-app state.
- A later claimed enabled-group run also failed to produce non-root direct reads.
- Whether that means Sequoia is stricter than Tahoe, or whether the permission was not effectively applied to the true host process, remains unresolved from local TCC evidence alone.

## Strongest Current Evidence

The following statements are already supported by local evidence:

- A non-root process on Sequoia can enumerate Apple internal SPU IMU services through the direct registry path.
- On the current probe, a non-root process cannot open the accel or gyro HID device through the direct path; the open call returns `0xe00002e2`.
- On the later claimed B-group run, the same direct open calls still returned `0xe00002e2`, and no IMU samples were read.
- Therefore, on this Sequoia machine, direct enumeration and direct read access are not equivalent.

The following statement is not yet supported and remains pending:

- Whether Sequoia allows direct non-root IMU reads when Input Monitoring is enabled for the exact effective host process used by the collector.

## Recommended Next Step

To finish the required A/B audit on this or another machine:

1. Enable Input Monitoring for the actual host app.
2. Restart the host app.
3. Re-run `EXP-0`, `EXP-2`, `EXP-3`, and `EXP-4`.
4. Save those results in a sibling directory, not over `current_probe/`.
5. Compare the enabled run against the current run and against README Tahoe.

## Transfer Note

If this repo is copied to another machine, preserve:

- the modified `exp0_device_discovery.py`
- the existing Tahoe `results/` directories
- this `results/sequoia_ab_20260313_233326/` directory

This keeps the Tahoe baseline, the Sequoia current probe, and the compatibility fix together for later comparison.
