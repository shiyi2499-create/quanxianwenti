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
import json
import sqlite3
import subprocess
import sys
from datetime import datetime

results = {
    "experiment": "EXP-4",
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "euid": os.geteuid(),
    "is_root": os.geteuid() == 0,
    "macos_version": None,
    "macos_build": None,
    "tcc_db_readable": False,
    "input_monitoring_entries": [],
    "console_tcc_deny_lines": [],
    "host_app_path": None,
    "host_app_name": None,
    "client_candidates": [],
    "process_chain": [],
    "runtime_context": {},
    "process_lookup_errors": [],
    "log_query_error": None,
    "recommendations": [],
}


def run_cmd(args):
    try:
        return subprocess.check_output(args, stderr=subprocess.DEVNULL, timeout=5).decode().strip()
    except Exception:
        return None


def collect_system_metadata():
    results["macos_version"] = run_cmd(["sw_vers", "-productVersion"])
    results["macos_build"] = run_cmd(["sw_vers", "-buildVersion"])


def collect_runtime_context():
    results["runtime_context"] = {
        "pid": os.getpid(),
        "ppid": os.getppid(),
        "sys_executable": sys.executable,
        "argv": sys.argv,
        "cwd": os.getcwd(),
        "term_program": os.environ.get("TERM_PROGRAM"),
        "shell": os.environ.get("SHELL"),
        "bundle_identifier": os.environ.get("__CFBundleIdentifier"),
    }


def load_process_table():
    for field in ("command=", "args=", "comm="):
        cmd = ["ps", "-axo", "pid=", "-o", "ppid=", "-o", field]
        out = run_cmd(cmd)
        if not out:
            results["process_lookup_errors"].append(f"ps returned no output for field {field}")
            continue

        proc_map = {}
        bad_lines = 0
        for line in out.splitlines():
            parts = line.strip().split(None, 2)
            if len(parts) < 3:
                bad_lines += 1
                continue
            try:
                pid = int(parts[0])
                ppid = int(parts[1])
            except ValueError:
                bad_lines += 1
                continue
            proc_map[pid] = {
                "pid": pid,
                "ppid": ppid,
                "command": parts[2].strip(),
                "source_field": field[:-1],
            }

        if proc_map:
            if bad_lines:
                results["process_lookup_errors"].append(
                    f"ignored {bad_lines} malformed ps lines for field {field}"
                )
            return proc_map

        results["process_lookup_errors"].append(f"ps parse yielded no rows for field {field}")

    return {}


def process_chain():
    chain = []
    seen = set()
    proc_map = load_process_table()
    pid = os.getpid()
    while pid > 1 and pid not in seen:
        seen.add(pid)
        info = proc_map.get(pid)
        if not info:
            results["process_lookup_errors"].append(f"missing pid {pid} in process table walk")
            break
        chain.append(info)
        pid = info["ppid"]
    return chain


def app_path_from_command(command):
    marker = ".app/Contents/"
    if marker not in command:
        return None
    idx = command.find(marker)
    return command[: idx + 4]


def collect_client_candidates(chain):
    candidates = set()

    term_program = os.environ.get("TERM_PROGRAM", "")
    if term_program:
        candidates.add(term_program)

    for entry in chain:
        command = entry["command"]
        app_path = app_path_from_command(command)
        if app_path:
            app_name = os.path.basename(app_path).replace(".app", "")
            candidates.add(app_name)
            candidates.add(app_name.lower())
            candidates.add(app_path)
            if results["host_app_path"] is None:
                results["host_app_path"] = app_path
                results["host_app_name"] = app_name
        base = os.path.basename(command)
        if base:
            candidates.add(base)
            candidates.add(base.lower())

    return sorted(candidates)


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
        completed = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
        if completed.returncode != 0:
            stderr_text = completed.stderr.decode("utf-8", errors="replace").strip()
            results["log_query_error"] = {
                "returncode": completed.returncode,
                "stderr": stderr_text,
            }
            print(f"    Cannot read system log: returncode={completed.returncode} stderr={stderr_text or '(empty)'}")
            return
        lines = completed.stdout.decode("utf-8", errors="replace").strip().split("\n")
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

    collect_system_metadata()
    collect_runtime_context()
    chain = process_chain()
    results["process_chain"] = chain
    results["client_candidates"] = collect_client_candidates(chain)

    # Step 1: Find likely host app / process context
    print(f"  macOS: {results['macos_version'] or 'unknown'} ({results['macos_build'] or 'unknown'})")
    print(f"  Host app: {results['host_app_path'] or results['host_app_name'] or 'unknown'}")
    print(f"  TERM_PROGRAM: {os.environ.get('TERM_PROGRAM', 'not set')}")
    print(f"  sys.executable: {results['runtime_context'].get('sys_executable') or 'unknown'}")
    print(f"  pid/ppid: {results['runtime_context'].get('pid')} / {results['runtime_context'].get('ppid')}")
    if results["client_candidates"]:
        print(f"  Client candidates: {', '.join(results['client_candidates'][:8])}")
    if chain:
        print(f"  Process chain depth: {len(chain)}")
    else:
        print(f"  Process chain depth: 0")
    if results["process_lookup_errors"]:
        print(f"  Process lookup notes:")
        for note in results["process_lookup_errors"][:5]:
            print(f"    - {note}")
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
    candidate_tokens = {c.lower() for c in results["client_candidates"] if c}
    has_host_permission = any(
        e.get("auth_value") == 2 and any(token in e.get("client", "").lower() for token in candidate_tokens)
        for e in results["input_monitoring_entries"]
    )
    if has_host_permission:
        print(f"    ✓ The current host app appears to have Input Monitoring permission.")
        print(f"    → If EXP-2 still fails as non-root, the restriction is kernel-level.")
        results["recommendations"].append(
            "Current host app has Input Monitoring. If EXP-2 still fails non-root, restriction is kernel-level."
        )
    else:
        print(f"    ⚠ The current host app is not confirmed in TCC Input Monitoring records.")
        print(f"    → Grant Codex or the actual host app in: System Settings > Privacy & Security > Input Monitoring")
        print(f"    → Then re-run EXP-2 as non-root to test if TCC was the blocker.")
        results["recommendations"].append(
            "Grant Codex or the actual host app Input Monitoring, then re-run EXP-2 non-root."
        )

    print(f"\n  Result JSON:")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
