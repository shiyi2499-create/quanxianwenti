# Tahoe 26 Non-Root IMU Evidence Snapshot

This directory stores the key tracked evidence for the confirmed `macOS 26.3 (25D125)` Tahoe result on the tested `Apple M4 / Mac16,12` machine.

## Why This Exists

The full local `results/` tree is gitignored. This snapshot preserves the minimum raw evidence needed to support the Tahoe conclusion directly inside the repository.

## Final Tahoe Claim Supported Here

On the tested Tahoe machine:

- a `non-root` process (`euid=501`) can discover the Apple internal SPU IMU
- a `non-root` process can open `AppleSPUHIDDevice` accel and gyro devices
- a `non-root` process can receive real accel and gyro callbacks
- `Input Monitoring` changes the `IOHIDManager` path
- but `Input Monitoring` is not required for the direct `AppleSPUHIDDevice` service path

## B Group: Input Monitoring Enabled

Files in [B_group](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group):

- [exp0_B.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group/exp0_B.log)
- [exp2_B.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group/exp2_B.log)
- [exp3_B.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group/exp3_B.log)
- [exp4_B.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group/exp4_B.log)
- [hidutil_B.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group/hidutil_B.log)

Key B-group points:

- `IOHIDManagerOpen(None) = 0x00000000`
- `IOHIDDeviceOpen(accel, None) = SUCCESS`
- `IOHIDDeviceOpen(gyro, None) = SUCCESS`
- `Accel callbacks: 100`
- `Gyro callbacks: 100`

## A Group: Input Monitoring Disabled

Files in [A_group](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group):

- [exp0_A.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group/exp0_A.log)
- [exp2_A_direct_service.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group/exp2_A_direct_service.log)
- [exp3_A.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group/exp3_A.log)
- [exp4_A.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group/exp4_A.log)
- [hidutil_A.log](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group/hidutil_A.log)

Key A-group points:

- `IOHIDManagerOpen(None) = 0xe00002e2`
- direct `AppleSPUHIDDevice` matching still succeeds
- `IOHIDDeviceOpen(accel, None) = SUCCESS`
- `IOHIDDeviceOpen(gyro, None) = SUCCESS`
- `Accel callbacks: 100`
- `Gyro callbacks: 100`

## What This Proves

The Tahoe result is not just "Input Monitoring on makes things work."

It shows a stronger split:

- manager path: permission-sensitive
- direct service path: still viable as non-root even with Input Monitoring disabled

That is the key first-layer result needed for Stage 2 raw IMU collection on Tahoe.
