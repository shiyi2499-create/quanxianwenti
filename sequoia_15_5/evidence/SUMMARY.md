# Sequoia Evidence Summary

This directory preserves the key Sequoia-specific evidence in a tracked, readable layout.

## Main conclusion supported here

On the tested `macOS 15.5 Sequoia` machine:

- a `non-root` IMU read path exists
- the working path is the direct `AppleSPUHIDDevice` plus `IOHIDDeviceOpen(...)` route
- under `Terminal.app`, the direct path still works even when `Input Monitoring` is disabled
- the main blocker observed so far is the `Codex` host context, not a blanket Sequoia OS prohibition

## Subdirectories

### [codex_host/current_probe](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/current_probe)

Initial Sequoia evidence showing:

- discovery works
- direct `IOHIDDeviceOpen(...)` fails in the `Codex` host context

### [codex_host/b_group_rerun](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/b_group_rerun)

Follow-up `Codex`-hosted rerun showing:

- direct open still fails
- runtime context points back to the `Codex` host chain

### [codex_host/exp6_probe](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe)

Event-system probe results showing:

- `Codex` host does not expose a readable HID event service list in the tested context

### [terminal_host/b_group](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group)

This is the first decisive success on Sequoia:

- `Terminal.app`
- `non-root`
- Input Monitoring enabled
- real accel/gyro callbacks received

### [terminal_host/a_group](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group)

This closes the Sequoia Terminal A/B loop:

- `Terminal.app`
- `non-root`
- Input Monitoring disabled
- direct path still succeeds

## Read order

1. [../CURRENT_CONCLUSION.md](/Users/shiyi/权限问题/sequoia_15_5/CURRENT_CONCLUSION.md)
2. [../RESULT_TIMELINE.md](/Users/shiyi/权限问题/sequoia_15_5/RESULT_TIMELINE.md)
3. [terminal_host/b_group/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/SUMMARY.md)
4. [terminal_host/a_group/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/SUMMARY.md)
5. then review the `codex_host/*` directories to understand why the earlier Sequoia picture looked more restrictive than it really was
