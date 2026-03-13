# Non-Root Phase 2 Equivalent Stream Bundle

This bundle records the March 14, 2026 Tahoe validation showing that the first-layer repository can emit a non-root IMU stream compatible with the second-stage collector's sensor-data needs.

## One-line conclusion

On this `macOS 26.3 Tahoe` machine, the first-layer direct `AppleSPUHIDDevice` path can produce the same `timestamp_ns + 6-axis IMU` stream shape expected by the second-stage project, at a stable `~200 Hz` rate, without root.

## What is included

- `code/`
  - the exact first-layer code used for this validation
- `raw_results/phase2_compat/`
  - five short repeated runs and one 60-second run
- `raw_results/exp2_nonroot_direct_outside_sandbox.log`
  - raw non-root direct-read proof log
- `raw_results/exp7_smoke.csv`
  - 3-second compatibility-smoke CSV output
- `raw_results/exp7_smoke.log`
  - raw smoke-test stdout log
- `notes/`
  - context, conclusion, requirement mapping, and commands used

## Most important files

1. `notes/CONCLUSION.md`
2. `raw_results/phase2_compat/summary.md`
3. `raw_results/phase2_compat/long_run.json`
4. `raw_results/exp2_nonroot_direct_outside_sandbox.log`
5. `raw_results/exp7_smoke.log`

## Interpretation boundary

This bundle proves:

- non-root IMU capture is possible through the first-layer direct SPU path
- the resulting stream matches the second-stage IMU schema and rate requirements

This bundle does not prove:

- that the current second-stage collector code runs unchanged without root

That second-stage implementation still uses the older `macimu`-based root-dependent backend.
