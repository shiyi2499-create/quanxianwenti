# Sequoia 15.5 IMU Audit Workspace

This directory is the isolated `macOS 15.5 Sequoia` workspace for the first-layer Apple internal IMU permission audit.

It exists so that Sequoia-specific experiments, scripts, and conclusions can evolve without changing the repository-root Tahoe baseline.

## Current Conclusion

On the tested `macOS 15.5 (24F74)` machine:

- a `non-root` process can read the Apple internal IMU
- the working path is the direct `AppleSPUHIDDevice` plus `IOHIDDeviceOpen(...)` route
- under `Terminal.app`, that direct path still works even after `Input Monitoring` is disabled
- the previously observed all-fail picture was primarily a `Codex` host-context problem, not a blanket Sequoia OS prohibition

Short version:

- Sequoia 15.5: `non-root IMU read exists`
- Terminal direct path: `Input Monitoring not required`
- Codex host: `major blocker / confounder`

See:

- [CURRENT_CONCLUSION.md](/Users/shiyi/权限问题/sequoia_15_5/CURRENT_CONCLUSION.md)
- [evidence/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/SUMMARY.md)

## What Is In This Directory

### Main scripts

- [exp0_device_discovery.py](/Users/shiyi/权限问题/sequoia_15_5/exp0_device_discovery.py)
- [exp1_macimu_patch_test.py](/Users/shiyi/权限问题/sequoia_15_5/exp1_macimu_patch_test.py)
- [exp2_iokit_imu.c](/Users/shiyi/权限问题/sequoia_15_5/exp2_iokit_imu.c)
- [exp3_open_modes.c](/Users/shiyi/权限问题/sequoia_15_5/exp3_open_modes.c)
- [exp4_tcc_check.py](/Users/shiyi/权限问题/sequoia_15_5/exp4_tcc_check.py)
- [exp5_background_persistence.py](/Users/shiyi/权限问题/sequoia_15_5/exp5_background_persistence.py)
- [exp6_event_system_probe.c](/Users/shiyi/权限问题/sequoia_15_5/exp6_event_system_probe.c)

### Host-context helper scripts

- [run_terminal_hosted_nonroot_suite.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_nonroot_suite.sh)
- [run_terminal_hosted_A_group.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_A_group.sh)

### Working notes

- [CODEX_HANDOFF.md](/Users/shiyi/权限问题/sequoia_15_5/CODEX_HANDOFF.md)
- [NEXT_STEPS_AFTER_EXP6.md](/Users/shiyi/权限问题/sequoia_15_5/NEXT_STEPS_AFTER_EXP6.md)
- [RESULT_TIMELINE.md](/Users/shiyi/权限问题/sequoia_15_5/RESULT_TIMELINE.md)
- [CODE_CHANGES.md](/Users/shiyi/权限问题/sequoia_15_5/CODE_CHANGES.md)

## Evidence Layout

Tracked Sequoia evidence is organized here:

- [evidence/codex_host/current_probe](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/current_probe)
- [evidence/codex_host/b_group_rerun](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/b_group_rerun)
- [evidence/codex_host/exp6_probe](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe)
- [evidence/terminal_host/b_group](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group)
- [evidence/terminal_host/a_group](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group)

How to read them:

1. Start with [evidence/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/SUMMARY.md)
2. Then read [RESULT_TIMELINE.md](/Users/shiyi/权限问题/sequoia_15_5/RESULT_TIMELINE.md)
3. Then inspect:
   - `codex_host/*` to understand the misleading all-fail stage
   - `terminal_host/b_group` to see the first successful non-root read
   - `terminal_host/a_group` to see that Input Monitoring was not required for the direct path

## What Changed Relative To The Root Baseline

The Sequoia workspace is intentionally close to the repository root baseline.

The main additions are:

- [exp6_event_system_probe.c](/Users/shiyi/权限问题/sequoia_15_5/exp6_event_system_probe.c)
  a Sequoia-only event-system probe used to test whether a non-`IOHIDDeviceOpen` path existed

- [run_terminal_hosted_nonroot_suite.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_nonroot_suite.sh)
  a controlled Terminal-hosted B-group runner

- [run_terminal_hosted_A_group.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_A_group.sh)
  a controlled Terminal-hosted A-group runner

See [CODE_CHANGES.md](/Users/shiyi/权限问题/sequoia_15_5/CODE_CHANGES.md) for the per-file summary.

## Most Important Findings

### 1. Codex-hosted Sequoia failure is real, but misleading if read as an OS-wide conclusion

Codex-hosted runs showed:

- direct `IOHIDDeviceOpen(...)` denied
- event-system service list hidden

But those runs were also visibly sandbox-constrained.

### 2. Terminal-hosted Sequoia non-root read succeeds

From:

- [evidence/terminal_host/b_group/exp2_terminal_nonroot.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/exp2_terminal_nonroot.log)

This is the strongest proof that Sequoia still allows a non-root readable IMU path.

### 3. Terminal-hosted A group also succeeds

From:

- [evidence/terminal_host/a_group/exp2_terminal_A.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/a_group/exp2_terminal_A.log)

This shows that, under `Terminal.app`, the direct path does not depend on `Input Monitoring`.

### 4. Event-system visibility is also host-context-sensitive

From:

- [evidence/codex_host/exp6_probe/SUMMARY.md](/Users/shiyi/权限问题/sequoia_15_5/evidence/codex_host/exp6_probe/SUMMARY.md)
- [evidence/terminal_host/b_group/exp6_terminal_nonroot.log](/Users/shiyi/权限问题/sequoia_15_5/evidence/terminal_host/b_group/exp6_terminal_nonroot.log)

`IOHIDEventSystemClient` could not expose a readable service list from Codex, but did expose services from Terminal.

## Remaining Open Question

The main unresolved question is no longer:

> "Does Sequoia allow any non-root IMU path?"

That is already answered: yes, it does.

The unresolved question is now:

> "Why does the Codex host context block a path that still works from Terminal on the same machine?"

The leading explanations are:

- Codex sandbox restrictions
- host-targeted permission differences
- or a combination of both
