# IMU Permission Audit — 实验对照矩阵 & 结果模板 & 论文措辞

## 一、实验对照矩阵

每行代表一次实验 run。请填写所有格子。

```
experiment_matrix.csv 字段设计:
run_id, experiment, macos_version, macos_build, chip, model,
euid, is_root, sip_status, tcc_input_monitoring,
app_signed, app_sandboxed, open_mode, iokit_return_code,
samples_read, effective_hz, verdict, notes
```

### 必测组合（最小对照集）

| Run | Exp  | euid | root | TCC InputMon | Open Mode | 预期结果 |
|-----|------|------|------|-------------|-----------|---------|
| 1   | EXP-0 | 501 | no  | N/A (enumerate) | N/A | 应能发现设备 |
| 2   | EXP-2 | 0   | yes | N/A | None | ✓ SUCCESS (baseline) |
| 3   | EXP-2 | 501 | no  | not granted | None | 测试目标 |
| 4   | EXP-2 | 501 | no  | granted | None | 测试目标 |
| 5   | EXP-2 | 501 | no  | not granted | Seize | 预期 FAIL |
| 6   | EXP-2 | 501 | no  | granted | Seize | 测试目标 |
| 7   | EXP-3 | 0   | yes | N/A | None+Seize | ✓ SUCCESS (baseline) |
| 8   | EXP-3 | 501 | no  | granted | None+Seize | 关键对照 |
| 9   | EXP-1 | 501 | no  | granted | (via macimu) | 测试 H1 |
| 10  | EXP-5 | 0   | yes | N/A | (via macimu) | 后台 Hz 退化 |

### 如果有多台机器，扩展维度

| 维度 | 变量值 |
|------|--------|
| 机型 | MacBook Air M2, MacBook Pro M3 Pro, MacBook Pro M4 |
| macOS | Sonoma 14.x, Sequoia 15.x |
| SIP | enabled (default), disabled (仅记录不建议) |

---

## 二、结果记录 CSV 模板

保存为 `results/experiment_matrix_filled.csv`：

```csv
run_id,experiment,timestamp,macos_version,macos_build,chip,model,euid,is_root,sip_status,tcc_input_monitoring,open_mode,iokit_return_code,samples_read,effective_hz,verdict,notes
1,EXP-0,2026-03-13T10:00:00Z,15.3,24D2072,M4,MacBookPro18x1,501,no,enabled,N/A,N/A,N/A,0,0,SUCCESS_SPU_DISCOVERED,
2,EXP-2,2026-03-13T10:05:00Z,15.3,24D2072,M4,MacBookPro18x1,0,yes,enabled,N/A,kIOHIDOptionsTypeNone,0x00000000,200,198.5,SUCCESS_ROOT_BASELINE,
3,EXP-2,2026-03-13T10:10:00Z,15.3,24D2072,M4,MacBookPro18x1,501,no,enabled,not_granted,kIOHIDOptionsTypeNone,FILL_IN,FILL_IN,FILL_IN,FILL_IN,
```

---

## 三、判定标准

### IOReturn 码速查

| 码 | 常量 | 含义 |
|---|------|------|
| `0x00000000` | kIOReturnSuccess | 成功 |
| `0xe00002e2` | kIOReturnNotPermitted | 权限拒绝 (TCC 或内核) |
| `0xe00002c5` | kIOReturnExclusiveAccess | 设备被独占 |
| `0xe00002bc` | kIOReturnBadArgument | 参数错误 |

### 判定树

```
EXP-2 non-root + kIOHIDOptionsTypeNone:
  → 0x00000000 + samples > 0  →  ★ H1 CONFIRMED: non-root read works
  → 0xe00002e2              →  OS-level deny (not just library gate)
    → 再测 TCC:
      → grant Input Monitoring → retry
        → 0x00000000         →  TCC was the blocker, not kernel
        → still 0xe00002e2   →  Kernel-level restriction
  → 0xe00002c5              →  Device seized by another process
  → other                   →  需要进一步调查
```

---

## 四、macimu Patch & Restore 脚本

### patch_macimu_root_gate.sh

```bash
#!/bin/bash
# 在 macimu 源码中注释掉 root 检查（可回滚）
# 用法: bash patch_macimu_root_gate.sh

MACIMU_PATH=$(python3 -c "import macimu; import os; print(os.path.dirname(macimu.__file__))")
echo "macimu location: $MACIMU_PATH"

# 查找包含 geteuid 的文件
TARGETS=$(grep -rl "geteuid" "$MACIMU_PATH" 2>/dev/null || true)
if [ -z "$TARGETS" ]; then
    echo "No geteuid check found in macimu. Nothing to patch."
    exit 0
fi

for f in $TARGETS; do
    echo "Patching: $f"
    cp "$f" "${f}.bak"  # Backup
    # Comment out the root check block
    # Strategy: replace 'if os.geteuid() != 0:' block with pass
    python3 -c "
import re, sys
with open('$f', 'r') as fh:
    code = fh.read()

# Pattern: if os.geteuid() != 0: ... raise ...
# Replace the entire if block with a comment
patched = re.sub(
    r'([ \t]*)if\s+os\.geteuid\(\)\s*!=\s*0\s*:.*?(?=\n\S|\n[ \t]*(?:def |class |#|$))',
    r'\1# [PATCHED] Root gate disabled for permission audit\n\1pass\n',
    code,
    flags=re.DOTALL
)

if patched != code:
    with open('$f', 'w') as fh:
        fh.write(patched)
    print(f'  ✓ Patched {len(code) - len(patched)} chars')
else:
    print('  ⚠ Pattern not matched; trying simpler replacement')
    patched2 = code.replace(
        'os.geteuid() != 0',
        'False  # PATCHED: was os.geteuid() != 0'
    )
    if patched2 != code:
        with open('$f', 'w') as fh:
            fh.write(patched2)
        print('  ✓ Simple patch applied')
    else:
        print('  ✗ Could not patch')
"
done
echo "Done. Restore with: bash restore_macimu_patch.sh"
```

### restore_macimu_patch.sh

```bash
#!/bin/bash
# 回滚所有 macimu patch
MACIMU_PATH=$(python3 -c "import macimu; import os; print(os.path.dirname(macimu.__file__))")
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
```

---

## 五、论文措辞模板

### 场景 A：Non-root 成功读取（高危发现）

> **Section 3.2: Sensor Access Permission Analysis**
>
> We investigate whether the Apple SPU IMU sensor requires elevated privileges for data access. The sensor, identified as a Bosch BMI286 unit exposed through `AppleSPUHIDDevice` in the IOKit registry, resides on a vendor-defined HID usage page (0xFF00) rather than the standard input device pages. Our experiments reveal that on macOS [VERSION] ([BUILD]), opening the sensor device via `IOHIDDeviceOpen` with `kIOHIDOptionsTypeNone` succeeds under a standard user account (euid 501) [when Input Monitoring permission is granted to the terminal application / without any special permissions]. This finding demonstrates that a non-privileged process—including a sandboxed application—can silently access IMU data at rates up to [X] Hz without triggering any user-visible permission dialog specific to motion sensors.
>
> This result significantly elevates the practical threat level of our attack, as a seemingly benign application (e.g., a fitness tracker or game) could covertly monitor keyboard vibrations through the IMU. Unlike microphone or camera access, macOS provides no dedicated permission category or visual indicator for continuous motion sensor access. We responsibly disclosed this finding to Apple Product Security on [DATE] (case ID: [ID]).

### 场景 B：Non-root 失败（系统限制，负结果）

> **Section 3.2: Sensor Access Permission Analysis**
>
> We systematically evaluate the privilege requirements for accessing the Apple SPU IMU sensor. Our experiments test multiple access paths: (1) Python-level library access via `macimu`, (2) direct C-level IOKit HID API calls with both non-exclusive (`kIOHIDOptionsTypeNone`) and exclusive (`kIOHIDOptionsTypeSeizeDevice`) open modes, and (3) with and without TCC Input Monitoring authorization.
>
> All non-root access attempts return `kIOReturnNotPermitted` (0xe00002e2), indicating that the `AppleSPUHIDDriver` enforces root-level access control at the kernel layer, independent of the TCC framework. This restriction applies even when the calling process has been granted Input Monitoring permissions. We note that this represents a more restrictive policy than standard USB HID devices, which can typically be accessed by non-root users on the same vendor usage page.
>
> **Implication for the threat model:** Our attack requires the adversary to obtain root execution on the target device. This can be achieved through privilege escalation exploits, supply chain compromise of a root-running daemon, or social engineering a user into running the malicious software with `sudo`. While this narrows the attack surface compared to a zero-privilege scenario, we note that macOS malware in the wild commonly achieves root access, and the absence of any user-visible indicator for IMU access means the attack remains covert once root is obtained.

### 场景 C：TCC 是唯一拦截点（中等发现）

> **Section 3.2: Sensor Access Permission Analysis**
>
> Our experiments reveal that non-root IMU access succeeds when the executing application has been granted "Input Monitoring" permission through the macOS TCC (Transparency, Consent, and Control) framework, and fails otherwise. Notably, the TCC dialog presented to users describes this permission as allowing the application to "monitor input from your keyboard"—it makes no mention of motion sensor or IMU access. This represents a permission scope mismatch: a user granting keyboard monitoring permission inadvertently authorizes silent IMU surveillance.
>
> This finding identifies a realistic attack vector: any application that legitimately requests Input Monitoring (e.g., keyboard remapping tools, accessibility software, text expanders) gains the ability to covertly read IMU data without additional authorization.

---

## 六、环境元信息记录清单

每次实验 run **必须**记录以下信息（自动采集见 `run_all_experiments.sh`）：

```
□ macOS 版本 (sw_vers -productVersion)
□ macOS build (sw_vers -buildVersion)
□ 芯片 (sysctl -n machdep.cpu.brand_string)
□ 机型 (sysctl -n hw.model)
□ Python 版本 (python3 --version)
□ macimu 版本 (pip show macimu | grep Version)
□ macimu git commit (if built from source)
□ SIP 状态 (csrutil status)
□ euid (id -u)
□ TCC Input Monitoring 状态 (EXP-4 output)
□ 实验开始 UTC 时间戳
```
