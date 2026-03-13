# Sequoia 15.5 EXP-6 Event-System Summary

Timestamp: 2026-03-14 00:54 Asia/Shanghai

## Scope

This run follows the Sequoia handoff rules:

- all Sequoia-specific work was done under `sequoia_15_5/`
- the repository root Tahoe baseline was not modified
- the priority task was compiling and running `exp6_event_system_probe.c`

## Environment

- macOS version: `15.5`
- macOS build: `24F74`
- chip/model: `Apple M4 / Mac16,12`
- euid: `501`
- host app context from `exp4`: `bundle_identifier = com.openai.codex`
- Python path from `exp4`: `/Library/Developer/CommandLineTools/usr/bin/python3`

Raw environment files:

- `env_sw_vers.txt`
- `env_hardware.txt`
- `env_arch.txt`
- `env_euid.txt`
- `env_sip.txt`

## Scripts Used

Script snapshots for this run are stored under:

- `scripts/CODEX_HANDOFF.md`
- `scripts/exp4_tcc_check.py`
- `scripts/exp6_event_system_probe.c`
- `scripts/SHA256SUMS.txt`

## EXP-4 Context Result

File:

- `exp4_context.log`

Result:

- runtime context confirms the run occurred inside Codex
- `bundle_identifier = com.openai.codex`
- `TCC.db` still unreadable in this sandboxed context
- `log show` still fails with `log: Cannot run while sandboxed`
- process-table reconstruction via `ps` still fails in this context

## EXP-6 Build Result

Files:

- `compile_exp6.log`
- `compile_exp6_rerun.log`
- `compile_exp6_rerun2.log`

Result:

- compile succeeded
- compile logs are empty because there were no warnings or errors

## EXP-6 Runtime Result

### Default client path

Files:

- `exp6_default.log`
- `exp6_default_rerun.log`
- `exp6_default_rerun2.log`

Most informative file:

- `exp6_default_rerun2.log`

Observed behavior:

- `IOHIDEventSystemClientCreateSimpleClient()` returned non-NULL
- `IOHIDEventSystemClientCopyServices()` returned NULL
- `IOHIDEventSystemClientCreate()` returned non-NULL
- `create` and `create+match` strategies also returned NULL service lists
- therefore no strategy produced a visible HID event service list

Meaning:

- in the tested Sequoia 15.5 non-root Codex context, the HID event-system path does not expose a readable service list for this probe

### Forced client-type path

Files:

- `exp6_client_type_0_rerun2.log`
- `exp6_client_type_1_rerun2.log`
- `exp6_client_type_2_rerun2.log`
- `exp6_client_type_3_rerun2.log`
- `exp6_client_type_4_rerun2.log`
- `exp6_client_type_5_rerun2.log`

Observed behavior:

- each run prints `calling IOHIDEventSystemClientCreateWithType(N)`
- no run prints `createWithType returned: ...`
- the process exits abnormally immediately after the call attempt

Meaning:

- in this context, `IOHIDEventSystemClientCreateWithType(0..5)` is not yielding a usable alternative probing route
- the failure happens inside or immediately after the framework call, before service enumeration

## Overall Interpretation

The previous Sequoia direct path result had already shown:

- `AppleSPUHIDDevice` discovery works
- `IOHIDDeviceOpen(...)` is denied with `0xe00002e2`

This new event-system run adds:

- the obvious event-system client constructors can be created as objects
- but they do not yield a visible service list in the tested non-root Codex context
- forcing client types 0 through 5 does not recover a usable path

## Final Conclusion For This Run

On `macOS 15.5 Sequoia` in the tested `non-root + Codex-hosted` context:

- no viable non-root IMU read path has been demonstrated through `IOHIDDeviceOpen(...)`
- no viable non-root IMU read path has been demonstrated through the current `IOHIDEventSystemClient` probe either

So the current answer is:

- **No alternative non-root readable IMU path has been established on Sequoia 15.5 from this event-system validation run.**

This is stronger than "not yet parsed":

- the event-system probe does not even reach a readable service list in the default path
- and the forced-type probes do not recover a working client path
