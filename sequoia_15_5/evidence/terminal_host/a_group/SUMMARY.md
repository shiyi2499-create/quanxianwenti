# Terminal-Hosted A Group Summary

Timestamp: 2026-03-14 01:25 Asia/Shanghai

## Verdict

A group **succeeded**.

Under:

- `macOS 15.5 (24F74)`
- `Terminal.app` host
- `non-root` (`euid=501`)
- user-reported `Input Monitoring disabled`

the process still successfully read the Apple internal IMU.

## Key Result

From `exp2_terminal_A.log`:

- `IOHIDManagerOpen(None) = 0xe00002e2`
- accelerometer direct service match: `FOUND`
- gyroscope direct service match: `FOUND`
- `IOHIDDeviceOpen(accel, None) = SUCCESS`
- `IOHIDDeviceOpen(gyro, None) = SUCCESS`
- accel callbacks: `100`
- gyro callbacks: `100`

So the direct `AppleSPUHIDDevice` path still produced real IMU samples in A group.

## Comparison Against Terminal-Hosted B Group

Reference B-group directory:

- `../terminal_hosted_nonroot_20260314_010715/`

Main difference:

- In B group, `IOHIDManagerOpen(None) = 0x00000000`
- In A group, `IOHIDManagerOpen(None) = 0xe00002e2`

What did **not** change:

- direct accel/gyro service discovery still worked
- direct `IOHIDDeviceOpen(..., None)` still succeeded
- real accel/gyro callbacks still arrived

Therefore the observable permission-sensitive step is the manager path, not the direct path.

## EXP-4 Note

`exp4_terminal_A.log` still cannot prove TCC state from `TCC.db`, because the database is unreadable in this context without extra access.

However:

- host context is clearly `Terminal.app`
- `TERM_PROGRAM = Apple_Terminal`
- no recent HID/SPU TCC deny lines were found

Combined with the B-to-A change in `IOHIDManagerOpen(...)`, the run is consistent with:

- Input Monitoring affecting the manager route
- but not being required for the direct Terminal-hosted IMU read path

## Final Answer

On `macOS 15.5 Sequoia`, under `Terminal.app` host:

- after disabling `Input Monitoring`, `non-root` **still can** read the Apple internal IMU through the direct `AppleSPUHIDDevice` path

So the Sequoia Terminal direct path does **not** depend on Input Monitoring.
