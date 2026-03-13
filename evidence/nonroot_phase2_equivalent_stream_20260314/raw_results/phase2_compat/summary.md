# Phase 2 Compatibility Summary

## Scope

This report tests whether the first-layer non-root direct SPU path can satisfy the second-stage collector's IMU needs without modifying the second-stage project.

## Second-stage requirements used for comparison

- sensor schema: `timestamp_ns,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z`
- single_key gate: `190.0 Hz`
- free_type gate: `150.0 Hz`
- timing model: `monotonic timestamp_ns aligned to key events in the collector`

## Short repeated trials

- run 1: effective=199.98Hz median=197.88Hz rows=1600 cluster=~200Hz single_key_gate=pass
- run 2: effective=199.95Hz median=197.76Hz rows=1600 cluster=~200Hz single_key_gate=pass
- run 3: effective=199.95Hz median=198.57Hz rows=1600 cluster=~200Hz single_key_gate=pass
- run 4: effective=199.99Hz median=198.57Hz rows=1600 cluster=~200Hz single_key_gate=pass
- run 5: effective=200.00Hz median=198.02Hz rows=1600 cluster=~200Hz single_key_gate=pass

## Long run

- duration=60.00s effective=199.98Hz median=198.47Hz rows=11999
- 5s window rate range: min=199.80Hz max=200.00Hz

## Verdict

- The first-layer non-root direct path demonstrates a Phase-2-compatible IMU capture path.
- It can emit the same `timestamp_ns + 6-axis` schema and reach the gating regime expected by the second-stage collector.
