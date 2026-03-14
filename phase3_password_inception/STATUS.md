# Current Status

## Scope

This folder is a Phase 3 experiment track for:

- `InceptionTime` baseline
- no-space continuous-string inference
- password-like threat model

It exists to avoid changing the main workspace under
[备份（mac_vs专用）](/Users/shiyi/备份（mac_vs专用）).

## What is already confirmed

1. The strongest visible Phase 2 server baseline is `InceptionTime`
   - [results_phase2.json](/Users/shiyi/备份（mac_vs专用）/results/服务器results/results_phase2.json)
   - `dl_InceptionTime = 0.8592`
   - `dl_Transformer = 0.8095`

2. The old sentence-style free_type route is not the right first target for the
   current attack story
   - it keeps `space/enter` in the label space
   - it uses a weaker backbone than the current best visible baseline
   - it evaluates sentence reconstruction rather than continuous-string recovery

3. The new no-space route runs end-to-end
   - [run_password_closure_inception.py](/Users/shiyi/权限问题/phase3_password_inception/run_password_closure_inception.py)
   - local smoke test completed successfully

## What has not been claimed yet

1. We are not claiming the local smoke-test accuracy is meaningful
2. We are not claiming sentence-level natural-language recovery is solved
3. We are not claiming fast-overlap typing is solved

## Immediate next step

Copy this folder to the server-side non-root trial workspace and run the
InceptionTime password/continuous-string closure on the real dataset:

- baseline training source: `single_key + boost`
- closure evaluation source: `trial_nonroot_free_type_refill`

## Expected output on the server

- `results/inception_password_final.pt`
- `results/inception_password_scaler.npz`
- `results/password_closure_inception.json`

The report should be interpreted using:

- exact sequence match
- positional char accuracy
- CER
