#!/bin/bash
# restore_macimu_patch.sh — 回滚 macimu 的所有 patch
set -euo pipefail

MACIMU_PATH=$(python3 -c "import macimu; import os; print(os.path.dirname(macimu.__file__))" 2>/dev/null)
if [ -z "$MACIMU_PATH" ]; then
    echo "✗ macimu not installed"
    exit 1
fi
echo "macimu location: $MACIMU_PATH"

BAKS=$(find "$MACIMU_PATH" -name "*.bak" 2>/dev/null)
if [ -z "$BAKS" ]; then
    echo "No .bak files found. Nothing to restore."
    exit 0
fi

for bak in $BAKS; do
    orig="${bak%.bak}"
    echo "Restoring: $orig"
    mv "$bak" "$orig"
done
echo "✓ All patches reverted."
