# Current Sequoia Conclusion

## Reliable conclusions

- `macOS 15.5 Sequoia` on this machine **does** have a `non-root` readable IMU path.
- The working path is the direct `AppleSPUHIDDevice` plus `IOHIDDeviceOpen(...)` route when launched from `Terminal.app`.
- For the `Terminal.app` direct path, `Input Monitoring` is **not required**.
- Disabling `Input Monitoring` changes the manager route (`IOHIDManagerOpen(...)`) but does not stop the direct path.

## What Codex host context means right now

- `Codex`-hosted runs failed where `Terminal.app`-hosted runs succeeded.
- So the earlier all-fail `Codex` picture should **not** be generalized into “Sequoia categorically blocks non-root IMU reads.”
- The strongest current interpretation is that `Codex` host context is a major blocker, likely due to sandbox and/or host-targeted permission differences.

## What is already settled

- Device discovery was never the main issue on Sequoia.
- The `Codex` host can distort both:
  - direct HID open results
  - event-system service visibility
- `Terminal.app` provides the decisive counterexample showing the OS still allows a non-root direct path.

## What is not fully settled yet

- Whether the `Codex` failure is caused primarily by:
  - sandboxing
  - TCC host-app mismatch
  - or a combination of both
- Whether the event-system path can also be made to return readable motion events, even though services are visible from Terminal.

## Short version

- Sequoia 15.5: `non-root IMU read exists`
- Terminal direct path: `Input Monitoring not required`
- Codex host: `major execution-context blocker`
