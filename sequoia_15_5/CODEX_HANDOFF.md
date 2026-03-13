# Sequoia 15.5 Codex Handoff

This document is the working handoff for the Codex instance running on the `macOS 15.5 Sequoia` machine.

Read this file before making any changes.

## Repository Rules

1. Do not modify the repository root experiments to chase Sequoia behavior.
2. Do all Sequoia-specific work only under:
   - [sequoia_15_5](/Users/shiyi/权限问题/sequoia_15_5)
3. The repository root is the preserved Tahoe baseline.
4. Tahoe conclusions are already committed and should be treated as fixed evidence.

## What Is Already Proven On Tahoe 26

These results are already established on `macOS 26.3 Tahoe`:

- non-root can discover Apple internal SPU IMU
- non-root can open accel and gyro via direct `AppleSPUHIDDevice` path
- non-root can read real accel/gyro callbacks
- `Input Monitoring` changes the `IOHIDManager` path
- but `Input Monitoring` is not required for the direct service path on Tahoe

Tracked evidence is here:

- [evidence/tahoe_26_nonroot/SUMMARY.md](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/SUMMARY.md)
- [evidence/tahoe_26_nonroot/A_group](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/A_group)
- [evidence/tahoe_26_nonroot/B_group](/Users/shiyi/权限问题/evidence/tahoe_26_nonroot/B_group)

Do not re-litigate the Tahoe result unless there is a direct contradiction.

## Current Sequoia State

Machine under test:

- macOS: `15.5`
- build: `24F74`
- chip/model: `Apple M4 / Mac16,12`
- user context: `euid=501`

Important Sequoia result bundles already available:

- [results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe)
- [results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/B_group_enabled_20260314_000033](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/B_group_enabled_20260314_000033)
- [results/B_group_rerun_20260314_002724](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724)

## Strongest Sequoia Facts Already Established

### 1. Device discovery is not the problem

See:

- [exp0_current.log](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe/exp0_current.log)
- [exp0_B_rerun.log](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724/exp0_B_rerun.log)

These show:

- `AppleSPUHIDDevice` is visible
- `usage 3` accel exists
- `usage 9` gyro exists
- direct service enumeration succeeds

So this is not a "matching logic can't find the IMU" problem.

### 2. The current failure point is `IOHIDDeviceOpen(...)`

See:

- [exp2_current.log](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe/exp2_current.log)
- [exp3_current.log](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe/exp3_current.log)
- [exp2_B_rerun.log](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724/exp2_B_rerun.log)
- [exp3_B_rerun.log](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724/exp3_B_rerun.log)

These consistently show:

- accel/gyro are `FOUND`
- `IOHIDDeviceOpen(..., kIOHIDOptionsTypeNone) = 0xe00002e2`
- `IOHIDDeviceOpen(..., kIOHIDOptionsTypeSeizeDevice) = 0xe00002e2`
- no callbacks are received

This means the current direct HID device-open path is being denied on Sequoia.

### 3. B-group rerun still failed

See:

- [SUMMARY.md](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724/SUMMARY.md)
- [exp4_B_rerun.log](/Users/shiyi/权限问题/results/B_group_rerun_20260314_002724/exp4_B_rerun.log)

New information from the rerun:

- `bundle_identifier = com.openai.codex`
- `sys.executable = /Library/Developer/CommandLineTools/usr/bin/python3`

So the run did happen under the Codex-hosted path, not some unrelated external host.

Still unresolved:

- `TCC.db` unreadable
- `ps` returns nothing in sandboxed app context
- `log show` fails with `log: Cannot run while sandboxed`

This means the TCC state is still not proven from local DB evidence.

### 4. There is a strong system-level hint that Sequoia is stricter

See:

- [ioreg_AppleSPUHIDDevice.txt](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe/ioreg_AppleSPUHIDDevice.txt)
- [ioreg_AppleSPUHIDDriver.txt](/Users/shiyi/权限问题/results/imu_audit_bundle_20260314/results/sequoia_ab_20260313_233326/current_probe/ioreg_AppleSPUHIDDriver.txt)

The accel/gyro `AppleSPUHIDDriver` services on Sequoia show:

- `motionRestrictedService = Yes`

This also appears on Tahoe for the accel/gyro drivers, so it is not by itself enough to explain the cross-version difference.
But it confirms these are motion-class services and that system-level motion restrictions are relevant.

## Current Interpretation

At this point, the best working interpretation is:

1. The old path
   - `AppleSPUHIDDevice`
   - `IOHIDDeviceCreate`
   - `IOHIDDeviceOpen`
   is viable on Tahoe
   but denied on Sequoia.

2. This no longer looks like a simple device-matching bug.

3. If Sequoia still has a non-root readable path, it is more likely to be:
   - `IOHIDEventSystemClient`
   - `IOHIDServiceClient`
   - `IOHIDServiceClientCopyEvent`
   or another event-service path,
   not the `IOHIDDeviceOpen` path.

## Sequoia-Specific Files Already Prepared

Work only with these files unless there is a good reason to add new ones:

- [exp0_device_discovery.py](/Users/shiyi/权限问题/sequoia_15_5/exp0_device_discovery.py)
- [exp2_iokit_imu.c](/Users/shiyi/权限问题/sequoia_15_5/exp2_iokit_imu.c)
- [exp3_open_modes.c](/Users/shiyi/权限问题/sequoia_15_5/exp3_open_modes.c)
- [exp4_tcc_check.py](/Users/shiyi/权限问题/sequoia_15_5/exp4_tcc_check.py)
- [exp6_event_system_probe.c](/Users/shiyi/权限问题/sequoia_15_5/exp6_event_system_probe.c)

### exp4_tcc_check.py status

[exp4_tcc_check.py](/Users/shiyi/权限问题/sequoia_15_5/exp4_tcc_check.py) has already been strengthened to record:

- `bundle_identifier`
- `sys.executable`
- `pid/ppid`
- runtime context
- process lookup errors
- sandbox-related `log show` failure details

This is important because Sequoia is running inside a more constrained Codex app context than Tahoe.

### exp6_event_system_probe.c status

[exp6_event_system_probe.c](/Users/shiyi/权限问题/sequoia_15_5/exp6_event_system_probe.c) is a new Sequoia-only probe.

Purpose:

- avoid `IOHIDDeviceOpen`
- try `IOHIDEventSystemClient`
- enumerate HID event services
- request sensor intervals
- attempt `IOHIDServiceClientCopyEvent`

Important caveat:

- It was smoke-tested for compileability on the Tahoe machine only.
- Runtime behavior on Tahoe is not evidence for Sequoia.
- It compiled successfully.
- Running it in the Codex app sandbox on Tahoe did not yield event services.
- Running it outside the app sandbox on Tahoe did enumerate services, but did not yet produce accel/gyro events.

Therefore:

- compile success is useful
- runtime conclusions must come from the Sequoia machine

## What The Sequoia Codex Should Do Next

Priority order:

### Task 1. Compile and run EXP-6 on Sequoia

Commands:

```bash
cd /Users/shiyi/权限问题/sequoia_15_5

clang -o exp6_event_system_probe exp6_event_system_probe.c \
  -framework IOKit -framework CoreFoundation -Wall -O2

./exp6_event_system_probe 2>&1 | tee exp6_default.log
```

Then also try forced client-type probing:

```bash
for t in 0 1 2 3 4 5; do
  echo "===== client type $t ====="
  ./exp6_event_system_probe --client-type "$t"
done 2>&1 | tee exp6_client_types.log
```

Interpretation:

- if no strategy yields `services visible`, the event-system path is blocked in current context
- if services are visible but no events are readable, the event-system path is present but not yielding motion events
- if any event types hit, continue deeper and parse them

### Task 2. If EXP-6 sees services, persist and inspect service properties

Add logging for:

- registry ID
- transport
- primary usage page / usage
- `motionRestrictedService`
- interval-related properties before/after set attempts

Goal:

- determine whether Sequoia exposes SPU motion services through the event system but refuses event reads
- or whether the event-system path is entirely hidden

### Task 3. If EXP-6 gets any events, focus on parsing before changing direction

Do not immediately rewrite the whole probe.
First answer:

- which event type returns non-null
- does it correlate with accel or gyro service
- does `CFCopyDescription(event)` mention motion/accel/gyro

### Task 4. If EXP-6 also fails completely, shift from “same path but different tuning” to “Sequoia genuinely stricter”

At that point, the likely conclusion becomes:

- Tahoe has a readable non-root path
- Sequoia 15.5 blocks both known non-root direct paths tested so far

## What Not To Waste Time On

1. Do not keep patching `exp2_iokit_imu.c` hoping a small `IOHIDDeviceOpen` change will suddenly work.
   That path has already failed consistently across multiple runs.

2. Do not interpret Tahoe runtime behavior as Sequoia evidence.

3. Do not touch the root-level Tahoe files unless there is a compelling reason and the user explicitly asks.

4. Do not overwrite old result directories.
   Always write into a new timestamped Sequoia results directory.

## Suggested Output Format For Future Sequoia Runs

For each meaningful new run, create a fresh result directory and save:

- `env_sw_vers.txt`
- `env_hardware.txt`
- `env_arch.txt`
- `env_euid.txt`
- `env_sip.txt`
- `exp4_*.log`
- `exp6_*.log`
- `SUMMARY.md`

If new source snapshots are relevant, include:

- `scripts/exp4_tcc_check.py`
- `scripts/exp6_event_system_probe.c`
- `scripts/SHA256SUMS.txt`

## Bottom Line For The Sequoia Codex

Your mission is not to re-prove Tahoe.

Your mission is to answer this narrower question:

> On macOS 15.5 Sequoia, after the known `IOHIDDeviceOpen` path is denied, is there still any viable non-root IMU read path through the HID event system or another adjacent motion-event interface?

That is the current frontier.
