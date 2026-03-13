# Experiment Context

- Date: 2026-03-14
- Machine under test: macOS 26.3 Tahoe (build 25D125)
- Effective UID during capture: non-root (`euid=501`)
- Goal: verify whether the first-layer non-root direct SPU path can emit the same IMU stream shape and rate regime required by the second-stage collector
- Important user-provided context: before this validation, the user disabled `Terminal.app` Input Monitoring
- Important execution note: the final `exp7` and captured `exp2` verification runs in this bundle were executed outside the Codex sandbox via unrestricted command execution, not by launching `Terminal.app` as the host application
- Therefore, the Terminal Input Monitoring state is recorded here as ambient context, but it is not the active gating variable for the `exp7` run itself
- What this bundle proves: a non-root direct `AppleSPUHIDDevice` path can provide a Phase-2-compatible IMU stream on this machine
- What this bundle does not claim: that the current second-stage collector implementation runs unchanged without root
