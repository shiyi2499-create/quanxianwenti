# Sequoia B Group Rerun Summary

Timestamp: 2026-03-14 00:28 Asia/Shanghai

## Scope

This directory records a rerun of the Sequoia B-group experiment after `exp4_tcc_check.py` was modified locally.

## Script Snapshot

The exact script versions used in this rerun are stored under:

- `scripts/exp0_device_discovery.py`
- `scripts/exp2_iokit_imu.c`
- `scripts/exp3_open_modes.c`
- `scripts/exp4_tcc_check.py`
- `scripts/SHA256SUMS.txt`

## Environment

- macOS version: `15.5`
- macOS build: `24F74`
- euid: `501`
- architecture: see `env_arch.txt`
- chip/model: see `env_hardware.txt`
- SIP: see `env_sip.txt`

## Results

### EXP-4

New information captured by the updated script:

- `sys.executable = /Library/Developer/CommandLineTools/usr/bin/python3`
- `bundle_identifier = com.openai.codex`
- `pid/ppid` captured successfully
- `log show` now reports explicit sandbox failure: `log: Cannot run while sandboxed`

Still unresolved:

- `ps` returned no usable output in the sandboxed app context
- `TCC.db` remained unreadable
- the script still could not positively confirm the effective host app permission record

### EXP-0

- `IOHIDManagerOpen(kIOHIDOptionsTypeNone) = 0xe00002e2`
- direct `AppleSPUHIDDevice` service enumeration succeeded
- accelerometer (`usage 3`) found
- gyroscope (`usage 9`) found

### EXP-2

- direct service match succeeded for accel and gyro
- `IOHIDDeviceOpen(accel, None) = 0xe00002e2`
- `IOHIDDeviceOpen(gyro, None) = 0xe00002e2`
- zero callbacks received

### EXP-3

- direct service match succeeded for accel and gyro
- `kIOHIDOptionsTypeNone = 0xe00002e2`
- `kIOHIDOptionsTypeSeizeDevice = 0xe00002e2`

## Bottom Line

This rerun does not change the practical access result:

- the IMU remains discoverable
- non-root direct open remains denied
- no IMU sensor samples were read

So on this rerun, the answer is still:

- `non-root` could **not** read IMU data on Sequoia 15.5
