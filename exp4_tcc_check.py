#!/usr/bin/env python3
"""
EXP-4: TCC (Transparency, Consent, Control) Status Check
==========================================================
macOS TCC 可能在 IOHIDDeviceOpen 时拦截非 root 访问（即使设备不是键盘）。
根据已知 bug 报告，TCC "Input Monitoring" 权限控制 ALL HID access，不限于键盘。

本实验检查:
  1. 当前终端 App (Terminal.app / iTerm / etc) 是否已获授 Input Monitoring 权限
  2. 尝试解读 TCC.db 的相关条目（需要 Full Disk Access 或 root）
  3. 记录 system.log / Console 中的 TCC deny 日志

判定标准:
  如果 non-root + Input Monitoring 已授权 → 可能成功绕过
  如果 non-root + 未授权 → TCC 可能是拦截点

用法:
  python3 exp4_tcc_check.py
  sudo python3 exp4_tcc_check.py
"""

import os
import sys
import json
import sqlite3
import subprocess
from datetime import datetime
from pathlib import Path

results = {
    "experiment": "EXP-4",
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "euid": os.geteuid(),
    "is_root": os.geteuid() == 0,
    "tcc_db_readable": False,
    "input_monitoring_entries": [],
    "console_tcc_deny_lines": [],
    "terminal_app_path": None,
    "recommendations": [],
}


def find_terminal_app():
    """Find the current terminal application path."""
    # Check common terminal apps
    candidates = [
        "/Applications/Utilities/Terminal.app",
        "/Applications/iTerm.app",
        "/Applications/Alacritty.app",
        "/Applications/kitty.app",
        "/Applications/Warp.app",
    ]
    # Also check TERM_PROGRAM env
    term = os.environ.get("TERM_PROGRAM", "")
    if term:
        results["terminal_app_path"] = term

    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def check_tcc_db():
    """Try to read TCC.db for InputMonitoring entries."""
    tcc_paths = [
        # User-level TCC database
        os.path.expanduser("~/Library/Application Support/com.apple.TCC/TCC.db"),
        # System-level (requires root or FDA)
        "/Library/Application Support/com.apple.TCC/TCC.db",
    ]

    for path in tcc_paths:
        if not os.path.exists(path):
            continue
        try:
            conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
            cursor = conn.cursor()
            # Look for ListenEvent (Input Monitoring) entries
            cursor.execute("""
                SELECT client, client_type, auth_value, auth_reason, last_modified
                FROM access
                WHERE service = 'kTCCServiceListenEvent'
                ORDER BY last_modified DESC
            """)
            rows = cursor.fetchall()
            conn.close()

            results["tcc_db_readable"] = True
            for row in rows:
                entry = {
                    "client": row[0],
                    "client_type": row[1],
                    "auth_value": row[2],  # 2 = allowed, 0 = denied
                    "auth_reason": row[3],
                    "last_modified": row[4],
                    "db_path": path,
                }
                results["input_monitoring_entries"].append(entry)
                auth_str = "ALLOWED" if row[2] == 2 else "DENIED" if row[2] == 0 else f"UNKNOWN({row[2]})"
                print(f"    {row[0]}: {auth_str} (type={row[1]}, reason={row[3]})")

            return True
        except sqlite3.OperationalError as e:
            print(f"    Cannot read {path}: {e}")
        except Exception as e:
            print(f"    Error reading {path}: {e}")

    return False


def check_console_logs():
    """Search recent system logs for TCC deny messages related to HID."""
    try:
        cmd = [
            "log", "show",
            "--predicate", 'subsystem == "com.apple.TCC" AND eventMessage CONTAINS[c] "deny"',
            "--last", "5m",
            "--style", "compact",
        ]
        out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, timeout=10)
        lines = out.decode("utf-8", errors="replace").strip().split("\n")
        hid_lines = [l for l in lines if "HID" in l.upper() or "SPU" in l.upper()]
        results["console_tcc_deny_lines"] = hid_lines[:20]  # cap at 20
        if hid_lines:
            print(f"    Found {len(hid_lines)} TCC deny lines mentioning HID/SPU")
            for l in hid_lines[:5]:
                print(f"      {l[:120]}")
        else:
            print(f"    No TCC deny lines for HID/SPU in last 5 minutes")
    except subprocess.TimeoutExpired:
        print(f"    log show timed out")
    except Exception as e:
        print(f"    Cannot read system log: {e}")


def main():
    print(f"  EXP-4: TCC Status Check")
    print(f"  euid: {os.geteuid()}")
    print()

    # Step 1: Find terminal app
    term_app = find_terminal_app()
    print(f"  Terminal app: {term_app or 'unknown'}")
    print(f"  TERM_PROGRAM: {os.environ.get('TERM_PROGRAM', 'not set')}")
    print()

    # Step 2: Check TCC database
    print(f"  --- TCC Database (Input Monitoring / kTCCServiceListenEvent) ---")
    if not check_tcc_db():
        results["recommendations"].append(
            "Cannot read TCC.db. Run with sudo or grant Full Disk Access to Terminal."
        )
        print(f"    ⚠ Cannot read TCC.db (need root or Full Disk Access)")
    print()

    # Step 3: Check system log for recent TCC denials
    print(f"  --- Recent TCC Deny Logs (last 5 min) ---")
    check_console_logs()
    print()

    # Step 4: Recommendations
    print(f"  --- Recommendations ---")
    has_terminal_permission = any(
        e.get("auth_value") == 2 and "terminal" in e.get("client", "").lower()
        for e in results["input_monitoring_entries"]
    )
    if has_terminal_permission:
        print(f"    ✓ Terminal has Input Monitoring permission.")
        print(f"    → If EXP-2 still fails as non-root, the restriction is kernel-level.")
        results["recommendations"].append(
            "Terminal has Input Monitoring. If EXP-2 fails non-root, restriction is kernel-level."
        )
    else:
        print(f"    ⚠ Terminal does NOT have Input Monitoring permission.")
        print(f"    → Grant it in: System Settings > Privacy & Security > Input Monitoring")
        print(f"    → Then re-run EXP-2 as non-root to test if TCC was the blocker.")
        results["recommendations"].append(
            "Grant Terminal Input Monitoring, then re-run EXP-2 non-root."
        )

    print(f"\n  Result JSON:")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
