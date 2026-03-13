#!/bin/zsh
set -u

BASE_DIR="/Users/shiyi/权限问题/sequoia_15_5"
TIMESTAMP="${1:-$(date '+%Y%m%d_%H%M%S')}"
RESULT_DIR="$BASE_DIR/results/terminal_hosted_A_${TIMESTAMP}"
SCRIPT_SNAPSHOT_DIR="$RESULT_DIR/scripts"

mkdir -p "$RESULT_DIR" "$SCRIPT_SNAPSHOT_DIR"

{
  echo "suite=terminal_hosted_A"
  echo "timestamp=$TIMESTAMP"
  echo "base_dir=$BASE_DIR"
  echo "result_dir=$RESULT_DIR"
  echo "launcher=$(whoami)"
  echo "shell=$SHELL"
  echo "term_program=${TERM_PROGRAM:-}"
  date '+started_at=%Y-%m-%dT%H:%M:%S%z'
} > "$RESULT_DIR/RUN_INFO.txt"

sw_vers > "$RESULT_DIR/env_sw_vers.txt"
sysctl -n machdep.cpu.brand_string > "$RESULT_DIR/env_hardware.txt" 2>/dev/null || system_profiler SPHardwareDataType > "$RESULT_DIR/env_hardware.txt"
uname -m > "$RESULT_DIR/env_arch.txt"
id -u > "$RESULT_DIR/env_euid.txt"
csrutil status > "$RESULT_DIR/env_sip.txt" 2>/dev/null || true
ps -o pid= -o ppid= -o comm= -p $$ > "$RESULT_DIR/env_shell_process.txt" 2>&1 || true

cp "$BASE_DIR/CODEX_HANDOFF.md" "$SCRIPT_SNAPSHOT_DIR/CODEX_HANDOFF.md"
if [[ -f "$BASE_DIR/NEXT_STEPS_AFTER_EXP6.md" ]]; then
  cp "$BASE_DIR/NEXT_STEPS_AFTER_EXP6.md" "$SCRIPT_SNAPSHOT_DIR/NEXT_STEPS_AFTER_EXP6.md"
fi
cp "$BASE_DIR/exp0_device_discovery.py" "$BASE_DIR/exp2_iokit_imu.c" "$BASE_DIR/exp4_tcc_check.py" "$SCRIPT_SNAPSHOT_DIR/"
shasum -a 256 "$SCRIPT_SNAPSHOT_DIR"/* > "$SCRIPT_SNAPSHOT_DIR/SHA256SUMS.txt"

cd "$BASE_DIR" || exit 1

python3 exp4_tcc_check.py > "$RESULT_DIR/exp4_terminal_A.log" 2>&1
exp4_exit=$?

clang -o exp2_iokit_imu exp2_iokit_imu.c -framework IOKit -framework CoreFoundation -Wall -O2 \
  > "$RESULT_DIR/compile_exp2.log" 2>&1
compile_exp2_exit=$?

./exp2_iokit_imu > "$RESULT_DIR/exp2_terminal_A.log" 2>&1
exp2_exit=$?

python3 exp0_device_discovery.py > "$RESULT_DIR/exp0_terminal_A.log" 2>&1
exp0_exit=$?

{
  echo "exp4_exit=$exp4_exit"
  echo "compile_exp2_exit=$compile_exp2_exit"
  echo "exp2_exit=$exp2_exit"
  echo "exp0_exit=$exp0_exit"
} > "$RESULT_DIR/EXIT_CODES.txt"

python3 - <<'PY' "$RESULT_DIR"
import os
import sys

result_dir = sys.argv[1]
with open(os.path.join(result_dir, "SUMMARY.stub.txt"), "w", encoding="utf-8") as fh:
    fh.write("Terminal-hosted A-group run finished.\n")
    fh.write("Inspect exp4_terminal_A.log, exp2_terminal_A.log, exp0_terminal_A.log.\n")

with open(os.path.join(result_dir, "DONE.txt"), "w", encoding="utf-8") as fh:
    fh.write("completed\n")
PY

osascript -e 'display notification "Sequoia Terminal A-group finished" with title "Codex"' >/dev/null 2>&1 || true

echo "DONE: $RESULT_DIR"
