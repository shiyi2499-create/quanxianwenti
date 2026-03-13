#!/usr/bin/env bash
# ==============================================================================
#  IMU Permission Audit — Master Runner
# ==============================================================================
#  本脚本为 macOS Apple Silicon IMU (BMI286 via AppleSPUHIDDevice) 的权限边界
#  研究提供完整可执行实验链路。
#
#  使用方法:
#    chmod +x run_all_experiments.sh
#    ./run_all_experiments.sh          # 非 root 运行（测 non-root 路径）
#    sudo ./run_all_experiments.sh     # root 运行（对照组）
#
#  ⚠ 不会修改系统安全策略（不禁用 SIP）
#  ⚠ 所有 patch 可通过 restore_macimu_patch.sh 回滚
# ==============================================================================

set -euo pipefail

# ── 环境元信息采集 ──────────────────────────────────────────────
RESULTS_DIR="results/imu_permission_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

ENV_FILE="$RESULTS_DIR/environment.json"
python3 -c "
import json, os, platform, subprocess, sys

def cmd(c):
    try:
        return subprocess.check_output(c, shell=True, stderr=subprocess.DEVNULL).decode().strip()
    except:
        return 'N/A'

env = {
    'timestamp':       '$(date -u +%Y-%m-%dT%H:%M:%SZ)',
    'macos_version':   platform.mac_ver()[0],
    'macos_build':     cmd('sw_vers -buildVersion'),
    'chip':            cmd('sysctl -n machdep.cpu.brand_string'),
    'machine_model':   cmd('sysctl -n hw.model'),
    'python_version':  sys.version.split()[0],
    'euid':            os.geteuid(),
    'is_root':         os.geteuid() == 0,
    'sip_status':      cmd('csrutil status'),
    'macimu_version':  'N/A',
    'macimu_location': 'N/A',
}

try:
    import macimu
    env['macimu_version'] = getattr(macimu, '__version__', 'unknown')
    env['macimu_location'] = os.path.dirname(macimu.__file__)
except ImportError:
    env['macimu_version'] = 'NOT_INSTALLED'

print(json.dumps(env, indent=2))
" > "$ENV_FILE"

echo "═══════════════════════════════════════════════════════"
echo "  IMU Permission Audit"
echo "═══════════════════════════════════════════════════════"
echo "  Results dir: $RESULTS_DIR"
echo "  Environment: $(cat $ENV_FILE | python3 -c 'import json,sys; d=json.load(sys.stdin); print(f"macOS {d[\"macos_version\"]} build {d[\"macos_build\"]}, {d[\"chip\"]}, euid={d[\"euid\"]}")')"
echo "═══════════════════════════════════════════════════════"
echo ""

# ── Experiment 0: IOKit 设备发现（不需要权限） ──────────────────
echo "▶ EXP-0: IOKit device discovery (no open)"
python3 exp0_device_discovery.py 2>&1 | tee "$RESULTS_DIR/exp0_discovery.log"
echo ""

# ── Experiment 1: macimu patch (去掉 root gate) ────────────────
echo "▶ EXP-1: macimu patched non-root test"
python3 exp1_macimu_patch_test.py 2>&1 | tee "$RESULTS_DIR/exp1_macimu_patch.log"
echo ""

# ── Experiment 2: 原生 C IOKit PoC ──────────────────────────────
echo "▶ EXP-2: Native C IOKit HID PoC"
if [ ! -f exp2_iokit_imu ]; then
    echo "  Compiling exp2_iokit_imu.c ..."
    clang -o exp2_iokit_imu exp2_iokit_imu.c \
        -framework IOKit -framework CoreFoundation \
        -Wall -O2 2>&1 | tee "$RESULTS_DIR/exp2_compile.log"
fi
if [ -f exp2_iokit_imu ]; then
    timeout 15 ./exp2_iokit_imu 2>&1 | tee "$RESULTS_DIR/exp2_native.log" || true
fi
echo ""

# ── Experiment 3: kIOHIDOptionsTypeNone vs SeizeDevice ─────────
echo "▶ EXP-3: Open mode comparison (None vs Seize)"
if [ ! -f exp3_open_modes ]; then
    echo "  Compiling exp3_open_modes.c ..."
    clang -o exp3_open_modes exp3_open_modes.c \
        -framework IOKit -framework CoreFoundation \
        -Wall -O2 2>&1 | tee "$RESULTS_DIR/exp3_compile.log"
fi
if [ -f exp3_open_modes ]; then
    timeout 10 ./exp3_open_modes 2>&1 | tee "$RESULTS_DIR/exp3_open_modes.log" || true
fi
echo ""

# ── Experiment 4: TCC Input Monitoring 检查 ─────────────────────
echo "▶ EXP-4: TCC Input Monitoring status"
python3 exp4_tcc_check.py 2>&1 | tee "$RESULTS_DIR/exp4_tcc.log"
echo ""

# ── Experiment 5: 后台/前台/锁屏采样持续性 ──────────────────────
echo "▶ EXP-5: Background/foreground persistence (requires root for now)"
echo "  (Run separately with: sudo python3 exp5_background_persistence.py)"
echo "  See exp5_background_persistence.py for details."
echo ""

# ── 汇总 ────────────────────────────────────────────────────────
echo "═══════════════════════════════════════════════════════"
echo "  All experiments complete."
echo "  Results: $RESULTS_DIR/"
echo "═══════════════════════════════════════════════════════"
