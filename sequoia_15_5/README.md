# Sequoia 15.5 Isolated Audit Workspace

This directory is the isolated `macOS 15.5 Sequoia` first-layer IMU audit workspace.

## Purpose

All future Sequoia-specific code changes should happen in this directory, not in the repository root.

This keeps the confirmed `Tahoe 26` baseline intact while allowing targeted compatibility and permission-debug changes for Sequoia.

## Source Baseline

This directory was copied from the repository root after the Tahoe result had already been committed and pushed.

Tahoe baseline commit:

- `b51e2cb` `Track Tahoe non-root IMU evidence snapshot`

## Scope

This directory currently contains the first-layer audit scripts and helpers:

- `exp0_device_discovery.py`
- `exp1_macimu_patch_test.py`
- `exp2_iokit_imu.c`
- `exp3_open_modes.c`
- `exp4_tcc_check.py`
- `exp5_background_persistence.py`
- `patch_macimu_root_gate.sh`
- `restore_macimu_patch.sh`
- `run_all_experiments.sh`

## Rule

- root-level Tahoe files remain the baseline
- Sequoia-only fixes should be implemented here
- if Sequoia-specific logs or notes are added later, they should stay under this directory or under a clearly Sequoia-scoped results path
