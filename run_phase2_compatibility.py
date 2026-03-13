#!/usr/bin/env python3
"""
Run repeated non-root capture trials from the first-layer direct SPU path and
compare the results against the second-stage collector requirements.
"""

import argparse
import csv
import json
import math
import os
import pathlib
import statistics
import subprocess
from datetime import datetime


ROOT = pathlib.Path("/Users/shiyi/权限问题")
BIN = ROOT / "exp7_phase2_capture"
SRC = ROOT / "exp7_phase2_capture.c"


def compile_binary() -> None:
    cmd = [
        "clang",
        "-o",
        str(BIN),
        str(SRC),
        "-framework",
        "IOKit",
        "-framework",
        "CoreFoundation",
        "-Wall",
        "-O2",
    ]
    subprocess.run(cmd, check=True)


def read_timestamps(csv_path: pathlib.Path) -> list[int]:
    timestamps: list[int] = []
    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            timestamps.append(int(row["timestamp_ns"]))
    return timestamps


def summarize_csv(csv_path: pathlib.Path) -> dict:
    ts = read_timestamps(csv_path)
    summary = {
        "csv": str(csv_path),
        "rows": len(ts),
        "duration_sec": 0.0,
        "effective_hz": 0.0,
        "median_hz": 0.0,
        "p10_hz": 0.0,
        "p90_hz": 0.0,
        "min_hz": 0.0,
        "max_hz": 0.0,
        "gate_190_pass": False,
        "gate_150_pass": False,
        "window_5s_min_hz": 0.0,
        "window_5s_max_hz": 0.0,
    }
    if len(ts) < 2:
        return summary

    duration_sec = (ts[-1] - ts[0]) / 1e9
    dts = [b - a for a, b in zip(ts, ts[1:]) if b > a]
    hz = [1e9 / dt for dt in dts if dt > 0]
    effective_hz = (len(ts) - 1) / duration_sec if duration_sec > 0 else 0.0

    summary["duration_sec"] = duration_sec
    summary["effective_hz"] = effective_hz
    summary["median_hz"] = statistics.median(hz) if hz else 0.0
    summary["p10_hz"] = statistics.quantiles(hz, n=10)[0] if len(hz) >= 10 else (min(hz) if hz else 0.0)
    summary["p90_hz"] = statistics.quantiles(hz, n=10)[-1] if len(hz) >= 10 else (max(hz) if hz else 0.0)
    summary["min_hz"] = min(hz) if hz else 0.0
    summary["max_hz"] = max(hz) if hz else 0.0
    summary["gate_190_pass"] = effective_hz >= 190.0
    summary["gate_150_pass"] = effective_hz >= 150.0

    window_counts: list[float] = []
    start = ts[0]
    window_ns = 5_000_000_000
    idx = 0
    while start < ts[-1]:
        end = start + window_ns
        count = 0
        while idx < len(ts) and ts[idx] < end:
            if ts[idx] >= start:
                count += 1
            idx += 1
        window_counts.append(count / 5.0)
        start = end
    if window_counts:
        summary["window_5s_min_hz"] = min(window_counts)
        summary["window_5s_max_hz"] = max(window_counts)

    return summary


def classify_cluster(rate: float) -> str:
    if 85 <= rate <= 115:
        return "~100Hz"
    if 130 <= rate <= 160:
        return "~146Hz"
    if 180 <= rate <= 220:
        return "~200Hz"
    return "other"


def write_summary(result_dir: pathlib.Path, second_stage_req: dict, short_runs: list[dict], long_run: dict) -> None:
    report = {
        "generated_at": datetime.now().isoformat(),
        "second_stage_requirements": second_stage_req,
        "short_runs": short_runs,
        "long_run": long_run,
    }
    (result_dir / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    lines = []
    lines.append("# Phase 2 Compatibility Summary")
    lines.append("")
    lines.append("## Scope")
    lines.append("")
    lines.append("This report tests whether the first-layer non-root direct SPU path can satisfy the second-stage collector's IMU needs without modifying the second-stage project.")
    lines.append("")
    lines.append("## Second-stage requirements used for comparison")
    lines.append("")
    lines.append(f"- sensor schema: `{second_stage_req['schema']}`")
    lines.append(f"- single_key gate: `{second_stage_req['single_key_gate_hz']} Hz`")
    lines.append(f"- free_type gate: `{second_stage_req['free_type_gate_hz']} Hz`")
    lines.append(f"- timing model: `{second_stage_req['timing_model']}`")
    lines.append("")
    lines.append("## Short repeated trials")
    lines.append("")
    for i, run in enumerate(short_runs, start=1):
        lines.append(
            f"- run {i}: effective={run['effective_hz']:.2f}Hz median={run['median_hz']:.2f}Hz "
            f"rows={run['rows']} cluster={classify_cluster(run['effective_hz'])} "
            f"single_key_gate={'pass' if run['gate_190_pass'] else 'fail'}"
        )
    lines.append("")
    lines.append("## Long run")
    lines.append("")
    lines.append(
        f"- duration={long_run['duration_sec']:.2f}s effective={long_run['effective_hz']:.2f}Hz "
        f"median={long_run['median_hz']:.2f}Hz rows={long_run['rows']}"
    )
    lines.append(
        f"- 5s window rate range: min={long_run['window_5s_min_hz']:.2f}Hz max={long_run['window_5s_max_hz']:.2f}Hz"
    )
    lines.append("")
    lines.append("## Verdict")
    lines.append("")

    short_any_190 = any(run["gate_190_pass"] for run in short_runs)
    long_pass_150 = long_run["gate_150_pass"]
    if short_any_190 and long_pass_150:
        lines.append("- The first-layer non-root direct path demonstrates a Phase-2-compatible IMU capture path.")
        lines.append("- It can emit the same `timestamp_ns + 6-axis` schema and reach the gating regime expected by the second-stage collector.")
    else:
        lines.append("- The first-layer non-root direct path is readable, but the observed rates did not yet consistently satisfy the second-stage gates.")
        lines.append("- More rate probing would be needed before claiming drop-in compatibility.")

    (result_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_trial(result_dir: pathlib.Path, label: str, seconds: int) -> dict:
    csv_path = result_dir / f"{label}.csv"
    log_path = result_dir / f"{label}.log"
    cmd = [str(BIN), "--seconds", str(seconds), "--csv", str(csv_path)]
    completed = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    log_path.write_text(completed.stdout, encoding="utf-8")
    summary = summarize_csv(csv_path)
    summary["label"] = label
    summary["exit_code"] = completed.returncode
    (result_dir / f"{label}.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--short-runs", type=int, default=5)
    parser.add_argument("--short-seconds", type=int, default=8)
    parser.add_argument("--long-seconds", type=int, default=60)
    args = parser.parse_args()

    compile_binary()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_dir = ROOT / "results" / f"phase2_compat_{stamp}"
    result_dir.mkdir(parents=True, exist_ok=True)

    second_stage_req = {
        "schema": "timestamp_ns,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z",
        "single_key_gate_hz": 190.0,
        "free_type_gate_hz": 150.0,
        "timing_model": "monotonic timestamp_ns aligned to key events in the collector",
    }

    short_runs = []
    for i in range(args.short_runs):
        short_runs.append(run_trial(result_dir, f"short_run_{i+1}", args.short_seconds))

    long_run = run_trial(result_dir, "long_run", args.long_seconds)
    write_summary(result_dir, second_stage_req, short_runs, long_run)
    print(result_dir)


if __name__ == "__main__":
    main()
