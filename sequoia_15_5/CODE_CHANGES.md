# Sequoia Code Changes

This note summarizes the current `sequoia_15_5/` tree relative to the repository root baseline.

## Files with meaningful Sequoia-specific additions

### [exp6_event_system_probe.c](/Users/shiyi/权限问题/sequoia_15_5/exp6_event_system_probe.c)

Purpose:

- add a Sequoia-only probe that avoids `IOHIDDeviceOpen(...)` and tries the `IOHIDEventSystemClient` / `IOHIDServiceClient` path
- expose detailed constructor logs so host-context failures can be localized more precisely

Current status:

- useful for showing that `Codex` and `Terminal` behave differently with respect to HID event-system visibility
- not currently the main data path, because direct `AppleSPUHIDDevice` reads already succeed from `Terminal.app`

### [run_terminal_hosted_nonroot_suite.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_nonroot_suite.sh)

Purpose:

- run the Sequoia probes from `Terminal.app` in a controlled, repeatable B-group host context
- capture environment files, script snapshots, compile logs, and result logs in a fresh result directory

### [run_terminal_hosted_A_group.sh](/Users/shiyi/权限问题/sequoia_15_5/run_terminal_hosted_A_group.sh)

Purpose:

- run the minimal `Terminal.app` A-group check after Input Monitoring is disabled
- preserve the exact Sequoia-local code snapshot alongside the logs

## Synced baseline files

These files were kept in sync with the root baseline or only had minor bookkeeping updates:

- [exp0_device_discovery.py](/Users/shiyi/权限问题/sequoia_15_5/exp0_device_discovery.py)
- [exp1_macimu_patch_test.py](/Users/shiyi/权限问题/sequoia_15_5/exp1_macimu_patch_test.py)
- [exp2_iokit_imu.c](/Users/shiyi/权限问题/sequoia_15_5/exp2_iokit_imu.c)
- [exp3_open_modes.c](/Users/shiyi/权限问题/sequoia_15_5/exp3_open_modes.c)
- [exp4_tcc_check.py](/Users/shiyi/权限问题/sequoia_15_5/exp4_tcc_check.py)
- [exp5_background_persistence.py](/Users/shiyi/权限问题/sequoia_15_5/exp5_background_persistence.py)
- [run_all_experiments.sh](/Users/shiyi/权限问题/sequoia_15_5/run_all_experiments.sh)

## Sequoia-only documentation added here

- [CURRENT_CONCLUSION.md](/Users/shiyi/权限问题/sequoia_15_5/CURRENT_CONCLUSION.md)
- [RESULT_TIMELINE.md](/Users/shiyi/权限问题/sequoia_15_5/RESULT_TIMELINE.md)
- [CODEX_HANDOFF.md](/Users/shiyi/权限问题/sequoia_15_5/CODEX_HANDOFF.md)
- [NEXT_STEPS_AFTER_EXP6.md](/Users/shiyi/权限问题/sequoia_15_5/NEXT_STEPS_AFTER_EXP6.md)

These exist to make the Sequoia workspace self-explanatory without touching the Tahoe root docs.
