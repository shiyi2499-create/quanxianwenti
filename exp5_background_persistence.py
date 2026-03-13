#!/usr/bin/env python3
"""
EXP-5: Background / Foreground / Lock-Screen Sampling Persistence
==================================================================
验证 H4: App Nap / 后台切换 / 锁屏是否中断 IMU 数据流。

方法:
  以 root 启动采集（确保基线可行），持续 120 秒。
  用户在此期间执行以下动作序列（手动，按终端提示）：
    T+0s:   前台运行（baseline）
    T+30s:  切到其他 App（模拟后台）
    T+60s:  切回终端
    T+90s:  锁屏（Ctrl+Cmd+Q），10s 后解锁
    T+110s: 结束

每秒记录一次采样率到 CSV，最终生成 Hz vs 时间 的数据。

用法:
  sudo python3 exp5_background_persistence.py
  # 按终端提示在 30s/60s/90s 时做对应操作

输出:
  results/exp5_persistence_{timestamp}.csv
  列: timestamp_s, epoch, samples_in_window, effective_hz, state_label
"""

import os
import sys
import csv
import time
import threading
from datetime import datetime

# ── Check root (this experiment needs actual sensor access) ─────
if os.geteuid() != 0:
    print("  EXP-5 requires root to establish baseline.")
    print("  Run: sudo python3 exp5_background_persistence.py")
    sys.exit(1)

try:
    from macimu import IMU
except ImportError:
    print("  macimu not installed. Run: pip install macimu")
    sys.exit(1)


DURATION_SEC = 120
CHECK_INTERVAL = 1.0

# State timeline (user follows prompts)
STATES = [
    (0,   "foreground"),
    (30,  "background"),
    (60,  "foreground_return"),
    (90,  "lock_screen"),
    (100, "unlock_return"),
]


def get_state_label(elapsed: float) -> str:
    label = "foreground"
    for t, l in STATES:
        if elapsed >= t:
            label = l
    return label


def main():
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_path = f"results/exp5_persistence_{ts}.csv"
    os.makedirs("results", exist_ok=True)

    print(f"═══════════════════════════════════════════════════════")
    print(f"  EXP-5: Background Persistence Test")
    print(f"  Duration: {DURATION_SEC}s")
    print(f"  Output: {out_path}")
    print(f"═══════════════════════════════════════════════════════")
    print()
    print("  INSTRUCTIONS (follow these during the test):")
    print("    T+ 0s: Keep this terminal in foreground (baseline)")
    print("    T+30s: Switch to another app (e.g. Finder)")
    print("    T+60s: Switch back to this terminal")
    print("    T+90s: Lock screen (Ctrl+Cmd+Q)")
    print("    T+100s: Unlock the screen")
    print("    T+120s: Test ends automatically")
    print()
    input("  Press ENTER to start...")

    # Start IMU
    imu = IMU()
    imu.__enter__()
    time.sleep(1)  # warm up

    # Sample counter
    sample_count = 0
    count_lock = threading.Lock()

    def drain_loop():
        nonlocal sample_count
        while not stop_event.is_set():
            try:
                samples = list(imu.read_accel())
                with count_lock:
                    sample_count += len(samples)
            except:
                pass
            time.sleep(0.005)

    stop_event = threading.Event()
    drain_thread = threading.Thread(target=drain_loop, daemon=True)
    drain_thread.start()

    # Record loop
    start = time.time()
    rows = []

    with open(out_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["elapsed_s", "epoch", "samples_in_window", "effective_hz", "state_label"])

        prev_count = 0
        while True:
            elapsed = time.time() - start
            if elapsed >= DURATION_SEC:
                break

            time.sleep(CHECK_INTERVAL)
            elapsed = time.time() - start

            with count_lock:
                cur = sample_count

            delta = cur - prev_count
            hz = delta / CHECK_INTERVAL
            state = get_state_label(elapsed)
            prev_count = cur

            row = [f"{elapsed:.1f}", f"{time.time():.3f}", delta, f"{hz:.1f}", state]
            writer.writerow(row)
            f.flush()

            # Progress
            bar_pos = int(elapsed / DURATION_SEC * 40)
            bar = "█" * bar_pos + "░" * (40 - bar_pos)
            prompt = ""
            for t, l in STATES:
                if abs(elapsed - t) < 1.5 and t > 0:
                    prompt = f" ← {l.upper()} NOW!"
            print(f"\r  [{bar}] {elapsed:5.1f}s  Hz={hz:6.1f}  state={state:20s}{prompt}",
                  end="", flush=True)

    stop_event.set()
    drain_thread.join(timeout=3)
    imu.__exit__(None, None, None)

    print(f"\n\n  ✓ Test complete. Data saved to: {out_path}")
    print(f"  Analyze: plot elapsed_s vs effective_hz, color by state_label")


if __name__ == "__main__":
    main()
