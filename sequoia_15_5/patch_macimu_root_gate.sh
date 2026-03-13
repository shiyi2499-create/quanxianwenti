#!/bin/bash
# patch_macimu_root_gate.sh — 注释掉 macimu 的 root 检查（可回滚）
# 用法: bash patch_macimu_root_gate.sh
set -euo pipefail

MACIMU_PATH=$(python3 -c "import macimu; import os; print(os.path.dirname(macimu.__file__))" 2>/dev/null)
if [ -z "$MACIMU_PATH" ]; then
    echo "✗ macimu not installed"
    exit 1
fi
echo "macimu location: $MACIMU_PATH"

TARGETS=$(grep -rl "geteuid" "$MACIMU_PATH" 2>/dev/null || true)
if [ -z "$TARGETS" ]; then
    echo "No geteuid check found in macimu. Nothing to patch."
    exit 0
fi

for f in $TARGETS; do
    echo "Patching: $f"
    cp "$f" "${f}.bak"
    # Replace the condition to always be False
    sed -i.sed_bak 's/os\.geteuid() != 0/False  # PATCHED: was os.geteuid() != 0/g' "$f"
    rm -f "${f}.sed_bak"
    echo "  ✓ Patched (backup: ${f}.bak)"
done
echo ""
echo "Done. Restore with: bash restore_macimu_patch.sh"
