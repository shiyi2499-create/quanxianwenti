# Terminal-Hosted Non-Root Summary

Timestamp: 2026-03-14 01:09 Asia/Shanghai

## Scope

This run isolates the host-context variable on the same Sequoia 15.5 machine:

- same machine: `macOS 15.5 (24F74)`
- same user: `euid=501`
- same Sequoia-specific code under `sequoia_15_5/`
- different host context: `Terminal.app` instead of `Codex`

## Host Context Evidence

From `exp4_terminal_nonroot.log`:

- `bundle_identifier = com.apple.Terminal`
- `TERM_PROGRAM = Apple_Terminal`
- process chain reconstruction succeeded
- parent chain includes `/System/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal`
- `log show` worked in this context and returned no recent HID/SPU TCC deny lines

This is a materially less constrained host context than the previous Codex-hosted runs.

## EXP-2 Result

File:

- `exp2_terminal_nonroot.log`

Observed result:

- `IOHIDManagerOpen(None) = 0x00000000`
- accelerometer and gyroscope were both found
- `IOHIDDeviceOpen(accel, None) = SUCCESS`
- `IOHIDDeviceOpen(gyro, None) = SUCCESS`
- accel callbacks: `100`
- gyro callbacks: `100`
- real IMU samples were printed

Meaning:

- on Sequoia 15.5, non-root IMU read is real when launched from Terminal in this run
- the OS is not categorically blocking non-root IMU reads on this machine

## EXP-6 Result

File:

- `exp6_terminal_nonroot.log`

Observed result:

- `IOHIDEventSystemClientCreateSimpleClient()` returned non-NULL
- `client strategy simple -> services visible`
- total HID event services visible: `143`
- SPU accel and gyro services were both matched
- but `IOHIDServiceClientCopyEvent(...)` returned no accel/gyro events in this polling probe

Meaning:

- the event-system path is also host-context-sensitive
- it is visible from Terminal but was hidden from the Codex-hosted context
- however, this specific event polling probe still did not yield readable motion events

## Codex vs Terminal Comparison

### Codex-hosted non-root

Previously established in `sequoia_15_5/results/exp6_event_probe_20260314_005149`:

- `exp2`: direct `IOHIDDeviceOpen(...)` denied with `0xe00002e2`
- `exp6`: event-system client could be created but no service list was visible

### Terminal-hosted non-root

This run shows:

- `exp2`: direct `IOHIDDeviceOpen(...)` succeeds and reads real IMU data
- `exp6`: event-system service list is visible

## Main Inference

The dominant difference is **host execution context**, not a blanket Sequoia OS prohibition.

The current evidence points to:

1. `Codex` host context is a major blocker on Sequoia 15.5.
2. The failure is not primarily explained by:
   - "Sequoia categorically blocks non-root IMU reads"
   - "the direct API path is fundamentally wrong"
3. TCC host-app targeting may still matter, but the stronger immediate finding is:
   - `Terminal.app` non-root succeeds where `Codex` non-root fails

## Direct Answer

On `macOS 15.5 Sequoia`, a non-root readable IMU path **does exist** on this machine.

Specifically:

- the direct `AppleSPUHIDDevice` plus `IOHIDDeviceOpen(...)` path works from `Terminal.app`
- therefore the previous all-fail picture was not mainly a pure OS-version denial
- the current primary blocker is the `Codex-hosted` execution context, likely involving sandbox and/or host permission targeting differences
