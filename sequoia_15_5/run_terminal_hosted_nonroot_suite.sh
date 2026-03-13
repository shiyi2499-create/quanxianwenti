#!/bin/zsh
set -u

BASE_DIR="/Users/shiyi/权限问题/sequoia_15_5"
TIMESTAMP="${1:-$(date '+%Y%m%d_%H%M%S')}"
RESULT_DIR="$BASE_DIR/results/terminal_hosted_nonroot_${TIMESTAMP}"
SCRIPT_SNAPSHOT_DIR="$RESULT_DIR/scripts"

mkdir -p "$RESULT_DIR" "$SCRIPT_SNAPSHOT_DIR"

{
  echo "suite=terminal_hosted_nonroot"
  echo "timestamp=$TIMESTAMP"
  echo "base_dir=$BASE_DIR"
  echo "result_dir=$RESULT_DIR"
  echo "launcher=$(whoami)"
  echo "shell=$SHELL"
  echo "term_program=${TERM_PROGRAM:-}"
  echo "cwd=$PWD"
  date '+started_at=%Y-%m-%dT%H:%M:%S%z'
} > "$RESULT_DIR/RUN_INFO.txt"

sw_vers > "$RESULT_DIR/env_sw_vers.txt"
system_profiler SPHardwareDataType > "$RESULT_DIR/env_hardware.txt"
id -u > "$RESULT_DIR/env_euid.txt"
csrutil status > "$RESULT_DIR/env_sip.txt"
uname -m > "$RESULT_DIR/env_arch.txt"
ps -o pid= -o ppid= -o comm= -p $$ > "$RESULT_DIR/env_shell_process.txt" 2>&1 || true

cp "$BASE_DIR/CODEX_HANDOFF.md" "$SCRIPT_SNAPSHOT_DIR/CODEX_HANDOFF.md"
if [[ -f "$BASE_DIR/NEXT_STEPS_AFTER_EXP6.md" ]]; then
  cp "$BASE_DIR/NEXT_STEPS_AFTER_EXP6.md" "$SCRIPT_SNAPSHOT_DIR/NEXT_STEPS_AFTER_EXP6.md"
fi
cp "$BASE_DIR/exp2_iokit_imu.c" "$BASE_DIR/exp4_tcc_check.py" "$BASE_DIR/exp6_event_system_probe.c" "$SCRIPT_SNAPSHOT_DIR/"
shasum -a 256 "$SCRIPT_SNAPSHOT_DIR"/* > "$SCRIPT_SNAPSHOT_DIR/SHA256SUMS.txt"

cd "$BASE_DIR" || exit 1

clang -o exp2_iokit_imu exp2_iokit_imu.c -framework IOKit -framework CoreFoundation -Wall -O2 \
  > "$RESULT_DIR/compile_exp2.log" 2>&1

clang -o exp6_event_system_probe exp6_event_system_probe.c -framework IOKit -framework CoreFoundation -Wall -O2 \
  > "$RESULT_DIR/compile_exp6.log" 2>&1

python3 exp4_tcc_check.py > "$RESULT_DIR/exp4_terminal_nonroot.log" 2>&1
exp4_exit=$?
./exp2_iokit_imu > "$RESULT_DIR/exp2_terminal_nonroot.log" 2>&1
exp2_exit=$?
./exp6_event_system_probe > "$RESULT_DIR/exp6_terminal_nonroot.log" 2>&1
exp6_exit=$?

{
  echo "exp4_exit=$exp4_exit"
  echo "exp2_exit=$exp2_exit"
  echo "exp6_exit=$exp6_exit"
} > "$RESULT_DIR/EXIT_CODES.txt"

python3 - <<'PY' "$RESULT_DIR"
import os
import subprocess
import sys

result_dir = sys.argv[1]

def exit_code_for_log(path):
    # The suite does not preserve process exit codes separately, so leave as unknown here.
    return "unknown"

summary = os.path.join(result_dir, "SUMMARY.stub.txt")
with open(summary, "w", encoding="utf-8") as fh:
    fh.write("Terminal-hosted non-root suite finished.\n")
    fh.write("Inspect exp4_terminal_nonroot.log, exp2_terminal_nonroot.log, exp6_terminal_nonroot.log.\n")

done_path = os.path.join(result_dir, "DONE.txt")
with open(done_path, "w", encoding="utf-8") as fh:
    fh.write("completed\n")
PY

osascript -e 'display notification "Sequoia Terminal suite finished" with title "Codex"' >/dev/null 2>&1 || true

echo "DONE: $RESULT_DIR"
