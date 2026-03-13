# Sequoia Result Timeline

## 1. Initial Codex-hosted Sequoia probe

Evidence:

- [evidence/codex_host/current_probe/TRANSFER_SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/current_probe/TRANSFER_SUMMARY.md)
- [evidence/codex_host/current_probe/exp2_current.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/current_probe/exp2_current.log)
- [evidence/codex_host/current_probe/exp4_current.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/current_probe/exp4_current.log)

What it showed:

- `AppleSPUHIDDevice` discovery worked
- accel/gyro services existed
- `IOHIDDeviceOpen(..., None)` failed with `0xe00002e2`
- the failure point was not device matching; it was the direct HID open call

Why it mattered:

- It established that Sequoia failure was happening after discovery, not before.

## 2. Codex-hosted B-group rerun

Evidence:

- [evidence/codex_host/b_group_rerun/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/b_group_rerun/SUMMARY.md)
- [evidence/codex_host/b_group_rerun/exp2_B_rerun.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/b_group_rerun/exp2_B_rerun.log)
- [evidence/codex_host/b_group_rerun/exp4_B_rerun.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/b_group_rerun/exp4_B_rerun.log)

What it showed:

- Even after the B-group rerun, `Codex`-hosted `non-root` still failed on the direct open path.
- `exp4` improved runtime identification and confirmed the run was happening under the `Codex` host path.
- But TCC DB evidence still remained unreadable in that app context.

Why it mattered:

- It made clear that the all-fail picture was specifically tied to the `Codex` execution context, not yet proven as a whole-OS rule.

## 3. EXP-6 event-system validation

Evidence:

- [evidence/codex_host/exp6_probe/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe/SUMMARY.md)
- [evidence/codex_host/exp6_probe/exp4_context.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe/exp4_context.log)
- [evidence/codex_host/exp6_probe/exp6_default_rerun2.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe/exp6_default_rerun2.log)

What it showed:

- In the `Codex`-hosted Sequoia context, `IOHIDEventSystemClient` constructors could be reached.
- But the default constructors did not yield a visible service list.
- Forced `CreateWithType(0..5)` did not recover a usable client path.

Why it mattered:

- It shifted the project away from “just try another API” and toward isolating host context as the main confounder.

## 4. Terminal-hosted B group

Evidence:

- [evidence/terminal_host/b_group/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/SUMMARY.md)
- [evidence/terminal_host/b_group/exp2_terminal_nonroot.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/exp2_terminal_nonroot.log)
- [evidence/terminal_host/b_group/exp4_terminal_nonroot.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/exp4_terminal_nonroot.log)
- [evidence/terminal_host/b_group/exp6_terminal_nonroot.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/exp6_terminal_nonroot.log)

What it showed:

- Same machine, same user, same Sequoia-local code
- only the host changed from `Codex` to `Terminal.app`
- `exp2` succeeded: non-root opened accel/gyro and read real IMU callbacks
- `exp6` also changed behavior: event-system services became visible from Terminal, although the current polling probe still did not read motion events

Why it mattered:

- This was the decisive host-context isolation step.
- It proved Sequoia 15.5 does have a non-root readable IMU path on this machine.

## 5. Terminal-hosted A group

Evidence:

- [evidence/terminal_host/a_group/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/SUMMARY.md)
- [evidence/terminal_host/a_group/exp2_terminal_A.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/exp2_terminal_A.log)
- [evidence/terminal_host/a_group/exp0_terminal_A.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/exp0_terminal_A.log)
- [evidence/terminal_host/a_group/exp4_terminal_A.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/exp4_terminal_A.log)

What it showed:

- After the user disabled `Terminal.app` Input Monitoring, the manager path changed:
  - `IOHIDManagerOpen(None)` became `0xe00002e2`
- But the direct `AppleSPUHIDDevice` path still succeeded:
  - `IOHIDDeviceOpen(accel/gyro, None) = SUCCESS`
  - real accel/gyro callbacks still arrived

Why it mattered:

- It shows that on Sequoia 15.5, under `Terminal.app`, the direct IMU read path does not depend on Input Monitoring.
