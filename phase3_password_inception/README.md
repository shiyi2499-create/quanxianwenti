# Phase 3 Password / Continuous-String Route

This folder isolates a no-space closure path that is closer to password-like
continuous string recovery than sentence reconstruction.

## Why this route exists

The previous free-type route in the main training workspace mixed together
several choices that are not ideal for the attack story we want to tell:

1. it used a `Transformer` backbone by default
2. it still carried `space` / `enter` in the training label space
3. it optimized for sentence-style decoding rather than continuous-string
   recovery

For the current paper story, the cleaner task is:

`non-root IMU access -> isolated-key baseline -> no-space continuous-string closure`

That is a better fit for password-like inputs and keeps the contribution focused
on attack feasibility rather than language reconstruction.

## Baseline choice

The server-side Phase 2 result snapshot shows the strongest visible baseline is
`InceptionTime`, not `Transformer`.

Source:
- [results_phase2.json](/Users/shiyi/备份（mac_vs专用）/results/服务器results/results_phase2.json)

Key accuracies from that snapshot:
- `dl_InceptionTime = 0.8592`
- `dl_Transformer = 0.8095`

So this route intentionally uses `InceptionTime` as the baseline backbone.

## Main script

- [run_password_closure_inception.py](/Users/shiyi/权限问题/phase3_password_inception/run_password_closure_inception.py)

What it does:

1. load `merged_dataset.npz` and train a final `InceptionTime` baseline
2. filter the classifier target space to `[a-z0-9]` only
3. ignore `space`, `enter`, `backspace`, and other non-password keys
4. build no-space sequences from free_type sessions using `typed_text`
5. evaluate:
   - exact sequence match
   - positional character accuracy
   - CER

## Expected data layout

Defaults assume the same layout used in the non-root trial workspace:

- `data/processed/merged_dataset.npz`
- `data/raw/trial_nonroot_free_type_refill/`

Override paths with CLI flags if needed.

## Important separation of roles

- `merged_dataset.npz` is still the main baseline training set
- it should come from `single_key + boost`
- `trial_nonroot_free_type_refill` is not the baseline training source
- it is used for no-space closure evaluation

In other words, this route does **not** train the baseline on free_type first.
It trains on isolated-key data, then checks whether that baseline can recover
continuous strings.

## Typical server run

```bash
python phase3_password_inception/run_password_closure_inception.py \
  --device cuda \
  --merged-path data/processed/merged_dataset.npz \
  --free-type-dirs data/raw/trial_nonroot_free_type_refill \
  --checkpoint-path results/inception_password_final.pt \
  --scaler-path results/inception_password_scaler.npz \
  --report-path results/password_closure_inception.json \
  --force-train
```

## Local smoke test status

This route has already been verified to run end-to-end in a local smoke test:

- checkpoint written successfully
- scaler written successfully
- report written successfully

Artifacts:
- [inception_password_final.pt](/Users/shiyi/权限问题/phase3_password_inception/results/inception_password_final.pt)
- [inception_password_scaler.npz](/Users/shiyi/权限问题/phase3_password_inception/results/inception_password_scaler.npz)
- [password_closure_inception.json](/Users/shiyi/权限问题/phase3_password_inception/results/password_closure_inception.json)

The local metrics are intentionally not used as a scientific result because the
smoke test was only:
- `1` epoch
- `256` training samples
- `2` evaluation sequences

Its purpose was just to verify that the code path runs to completion.
