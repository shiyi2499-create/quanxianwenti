# Next Thinking After EXP-6

This note is for the Codex instance working on the `macOS 15.5 Sequoia` machine.

Read this after reading [CODEX_HANDOFF.md](/Users/shiyi/权限问题/sequoia_15_5/CODEX_HANDOFF.md).

## What EXP-6 Changed

The latest result bundle is:

- `/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149`

Key files:

- [SUMMARY.md](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/SUMMARY.md)
- [exp4_context.log](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/exp4_context.log)
- [exp6_default.log](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/exp6_default.log)
- [exp6_default_rerun2.log](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/exp6_default_rerun2.log)
- [exp6_client_type_0_rerun2.log](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/exp6_client_type_0_rerun2.log)
- [exp6_client_type_5_rerun2.log](/Users/shiyi/权限问题/results/exp6_event_probe_20260314_005149/exp6_client_type_5_rerun2.log)

What these results now establish:

1. `IOHIDDeviceOpen(...)` path is still denied on Sequoia.
2. `IOHIDEventSystemClient` default constructors also do not produce a readable service list in the current `non-root + Codex-hosted` context.
3. `CreateWithType(0..5)` does not recover a usable path in the current context.
4. `exp4` continues to show the run is happening inside `com.openai.codex`, and also shows the environment is sandbox-constrained enough that:
   - `ps` reconstruction fails
   - `log show` fails with `log: Cannot run while sandboxed`
   - `TCC.db` cannot be inspected

## The Most Important Thinking Shift

Do **not** think:

> "We just need one more API tweak."

Think:

> "We have multiple overlapping gates, and we now need to isolate them one by one."

The most likely gates are:

1. Sequoia policy difference
2. Codex host sandbox / execution context
3. TCC / Input Monitoring applied to the wrong host
4. API-family difference

Right now, the biggest unresolved confounder is:

**current Sequoia results are all from a Codex-hosted context that is visibly sandbox-constrained**

So the next most important variable to isolate is **host execution context**, not another random API.

## How To Think About The Problem

Treat the problem as a matrix:

### Axis A: Host Context

- Codex-hosted context
- Terminal.app context
- iTerm2 context, if available

### Axis B: Permission State

- Input Monitoring disabled
- Input Monitoring enabled for the actual host app

### Axis C: Privilege

- non-root
- root

### Axis D: API Path

- `IOHIDDeviceOpen(...)`
- `IOHIDEventSystemClient`
- any later adjacent path only after A/B/C are isolated

Do **not** vary all four axes at once.

## Highest-Value Next Experiment

The highest-value next step is:

### Re-run the exact same Sequoia probes from plain Terminal outside Codex

Why:

- Current evidence strongly suggests Codex context itself is materially constrained.
- `exp4_context.log` explicitly says `log: Cannot run while sandboxed`.
- `ps` returns no usable process data in this context.
- `IOHIDEventSystemClient` not returning services may be a sandbox/context issue, not a pure Sequoia kernel policy issue.

So before inventing a third API family, answer this:

> Does the same non-root user get a different result when the executable is launched from Terminal.app instead of Codex?

## Concrete Priority Order

### Priority 1. Terminal-hosted non-root replication

Run from plain `Terminal.app`, not inside Codex if possible.

Do this first:

1. Run `exp4_tcc_check.py`
2. Run `exp2_iokit_imu`
3. Run `exp6_event_system_probe`

Both:

- with Input Monitoring enabled for `Terminal.app`
- and, if safe, with it disabled

Goal:

- isolate host-context effect

Interpretation:

- if Terminal non-root succeeds where Codex fails:
  - the problem is not "Sequoia categorically blocks everything"
  - the problem is at least partly Codex host context / sandbox

- if Terminal also fails identically:
  - the Sequoia restriction claim becomes much stronger

### Priority 2. Outside-sandbox execution of the same binary

If Codex can request unrestricted execution for a local command, compare:

- same binary
- same user
- same machine
- same permission state
- only execution context changes

This is very high signal.

### Priority 3. Root controls

If non-root continues to fail in both Codex and Terminal, run root controls:

- `sudo ./exp2_iokit_imu`
- `sudo ./exp6_event_system_probe`

Goal:

- distinguish "non-root gate" from "path fundamentally broken"

Interpretation:

- if root succeeds:
  - there is a privilege gate, not a dead path

- if root also fails:
  - the issue is deeper than user privilege and current API choice

### Priority 4. Only then consider deeper API expansion

Only after host-context and root controls are isolated should you consider:

- event callbacks instead of polling
- HID.framework / private framework entry points
- alternate user clients

Do not jump there too early.

## What To Avoid

1. Do not keep changing `exp2_iokit_imu.c` hoping `IOHIDDeviceOpen` will suddenly work.
2. Do not interpret another failed Codex-hosted run as proof that no non-root path exists anywhere on Sequoia.
3. Do not add many new APIs before isolating host context.
4. Do not assume Input Monitoring is truly granted to the effective host just because the user says it was enabled.

## Decision Tree

### Case 1: Terminal non-root succeeds

Then conclude:

- Codex-hosted context is a major blocker
- Sequoia itself may still allow non-root access in a less constrained host

Next move:

- document Codex-vs-Terminal difference clearly
- stop over-generalizing current Codex failures to the entire OS

### Case 2: Terminal non-root fails, but root succeeds

Then conclude:

- Sequoia likely enforces a stronger privilege boundary than Tahoe
- the key question becomes whether this is TCC-only or broader non-root denial

Next move:

- strengthen host permission evidence
- compare exact deny points across non-root and root

### Case 3: Terminal non-root fails and root also fails

Then conclude:

- either current APIs are wrong for Sequoia
- or Sequoia moved the readable path elsewhere entirely

Next move:

- then, and only then, escalate to broader private HID event paths

## Recommended Message To Keep In Mind

The project is no longer asking:

> "Can we make one C program work?"

It is now asking:

> "What is the real boundary on Sequoia: OS version, privilege, TCC, host sandbox, or API family?"

That is the right framing for the next phase.
