<div align="center">

# Kin

**CPU 调度守护进程**

省电优先 · 智能自学习 · 应用画像 · governor 自主调频

[简体中文](README.md) | [English](README_EN.md)

[![License](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](#协议)
[![SoC](https://img.shields.io/badge/SoC-SM8850%20·%208e5-red.svg)](#)
[![Governor](https://img.shields.io/badge/Governor-walt%20·%20schedutil%20·%20hmbird-brightgreen.svg)](#)
[![App](https://img.shields.io/badge/前端-接管助手-orange)](https://github.com/kktwo2923-creator/Kin)

</div>

---

## 这是什么

**Kin** 是面向 Android 平台的 CPU 调度守护进程:
它设定各模式的频率上下限与 governor 参数,把实际的升降频节奏交给内核 governor(walt / schedutil / hmbird)自己完成 —— 既省电又跟手;智能模式下再叠加负载状态机与帧感知闭环。

可单独用 `mode.txt` 切换模式;也可搭配图形前端 [Kin 接管助手](https://github.com/kktwo2923-creator/Kin) 按应用画像自动接管。

- **多 SoC 支持**:骁龙 8 至尊版(8e5/8E)/8G1/8G2/8G3/8G5/7P3 · 天玑 9300/9400(内置 10 份出厂配置,刷入时按机型自动选择)

## 核心特性

- 🪶 **极简调频模型** —— 只管上下限 + governor 参数,频率响应交给 governor
- 🔋 **五种模式** —— powersave / balance / performance / fast(风驰) / auto(智能自学习),一行命令即时切换
- 🎯 **应用画像** —— 按包名匹配模型,游戏自动拉满,退出自动还原
- 🧠 **智能模式(auto)** —— 六状态负载 FSM + 省电倾向自学习 + 夜间作息学习 + 问卷纠正;
  每状态频率上限可自定义(fsm_policy),按应用分类封顶/关核省电(app_policy)
- 🎮 **FAS 帧感知调频** —— 游戏内按实际帧率闭环调 scaling_max:帧稳降频省电,掉帧秒放开追帧
- 🌀 **风驰调度交接** —— 「风驰游戏」档解除 Kin 的频率上限并放权 governor，由 ROM/风驰(hmbird/scx 等)接管；Kin 不强制写兜底 governor，与 FAS 按应用互斥
- ⚙️ **governor 自主调频** —— 用 `up_rate_limit_us` / `down_rate_limit_us` / `hispeed_load` 精细控制升降频节奏
- 📉 **真正能降频** —— 修复退出游戏后 SchedParam 不还原、场景频繁重写上限导致 CPU 粘高频的老问题
- 🧩 **配置精简** —— 常用模块默认开启,config 只写需要覆盖的部分
- **系统级调优(移植 ASB)** —— 每档 VM/网络/WiFi 调优 + BG_TRIM 后台内存回收

### 系统级优化（移植自 AutoSystemBoost，非 CPU 部分）

由 `Function` 三个开关控制（缺省即用内置默认，无需逐档配频率）：

| 开关 | 默认 | 作用（ASB 真值） |
|---|---|---|
| `SysTune.enable` | **开** | 每档 VM(`swappiness`/`dirty_*`/`vfs_cache_pressure`) + 网络(`tcp_rmem/wmem`/`fastopen`/`keepalive`/V57 TCP 与缓冲节点) + WiFi(`power_save`/`tx_queue_len`)。省电档省、性能档激进；仅写已存在节点，缺失能力跳过并记录，重启自动还原。 |
| `BootTrim.enable` | 已停用 | 旧开关保留兼容，不再永久禁用系统包或修改 WiFi 扫描设置。 |
| `BgTrim.enable` | 关 | 周期性软回收后台内存(`am send-trim-memory`)：重度社交/流媒体常驻回收，息屏再回收相册/音乐工作集。**绝不杀进程、不动前台/系统应用**(同 ASB)。`interval_s` 默认 90。 |

> 不移植 ASB 的音频/相机 vendor 分区改写——那属另一类模块，不塞进 CPU 调度器。

Wi-Fi 节能接管前先读取原值；切回均衡档、关闭 SysTune 或守护正常退出时，仅在当前值仍由 Kin 持有时恢复。读不到原值则跳过接管；恢复失败保留待恢复状态。强制终止或设备断电不保证执行恢复。

SysTune 的 VM、TCP 和发送队列节点也记录原值，关闭 SysTune 或正常退出时条件恢复，失败由现有巡检重试；不再镜像 ROM 属性。恢复记录仅在进程内，尚不支持崩溃后的持久化重放。`post-fs-data.sh` 不再设置系统属性或使用 bind-mount 禁用 GPU/帧提频。旧版写入的持久属性和禁用包不能凭猜测恢复，需要按原始配置核对。

模块不会在开机时删除其他模块；如有调度冲突，请在模块管理器中自行停用冲突项。

安装升级保留已有 `config.json` 和 App 数据，APK 签名冲突时不自动卸载旧 App；卸载模块保留 Kin 的应用策略和学习数据。首次安装才写入默认主配置。

启动脚本不再伪造外壳温度、停止 horae 或遮蔽厂商限频接口。从旧版升级后需要重启，才能清除旧启动脚本已经建立的挂载和运行时修改。未经过真机验收的构建应先备份再测试。

本地打包：设置 `ANDROID_NDK` 后运行 `python build_pack.py`，直接从新编译的 `output/Kinetic` 生成 ZIP；`pack` 子命令也只使用该路径，不回退到仓库内的旧二进制。默认沿用内置 APK，指定 `KIN_APK` 可显式替换。

> **设计思路**参考 [yumi](https://github.com/imacte/yumi) 与 [MW_CpuTurboScheduler](https://github.com/MoWei-2077/MW_CpuTurboScheduler)。
>
> **无场景识别**:不再做 Scenes / Standby / HeavyLoad / Touch 检测,频率响应统一交给 governor 的 `up_rate_limit_us` / `down_rate_limit_us`。

---

## 构建

前置:Android NDK(`ANDROID_NDK` 环境变量指向 NDK 目录)、CMake ≥ 3.22、Python 3(打包用)。

```bash
export ANDROID_NDK=/path/to/android-ndk   # 未设置时 build.sh 会直接报错退出
./build.sh                  # 编译 arm64-v8a 二进制到 output/arm64/Kinetic
python build_pack.py pack   # 与 magisk/ 模块源打包成可刷入的 zip(output/ 下)
```

`build.sh` 读取 `BUILD_TYPE`(默认 Release)与 `ANDROID_PLATFORM`(默认 android-29)。
仓库根目录的 `Kinetic` 是预编译二进制,`pack` 会直接用它;自己改过源码请先跑 `build.sh`。

## 快速上手

### 1. 文件位置
```
/sdcard/Android/Kin/
├── config.json      # 主配置
├── mode.txt         # 当前模式
├── log.txt          # 运行日志
└── AI/
    ├── auto_status.txt   # 智能策略实时快照
    └── capabilities.txt  # 设备能力摘要
```

### 2. 切换模式（立即生效）
```bash
echo "powersave"   > /sdcard/Android/Kin/mode.txt   # 最省电（关大核）
echo "balance"     > /sdcard/Android/Kin/mode.txt   # 日常用 ← 推荐
echo "performance" > /sdcard/Android/Kin/mode.txt   # 性能优先
echo "fast"        > /sdcard/Android/Kin/mode.txt   # 风驰（hmbird 调速器）
echo "auto"        > /sdcard/Android/Kin/mode.txt   # 自动（按温度/电量/时段自学习切档）
```

---

## 自动模式（auto）——自适应自学习

这里的“自学习”是基于时段、温度和电流反馈的启发式自适应，不是经过同负载对照验证的能效最优化。不同应用、亮度和网络条件会影响会话均流，奖励信号不能单独证明调参带来省电或性能收益。效果需要在具体设备上用相同工作负载验证。

`auto` 的**学习/决策算法移植自 [AutoSystemBoost](https://github.com/krilikd/AutoSystemBoost) 的
Smart Mode**（真实源码 `asb_smart_defs.h` / `asb_smart.h`，MIT，© Dima Krylov），
忠实复刻其 12 桶学习、置信门控、分级回退、夜间/热否决/低电覆盖与阈值常量；
但**适配 Kin：输出离散映射到 `powersave / balance / performance` 三个基础档**，并由 FSM/FAS 在需要时提供动态频率边界；
边界以内仍交给 governor 调频，不做高频无意义重写，也不会重蹈频繁改
`scaling_max_freq` 导致不降频的坑。它只决定"无专属画像时"的基础档——游戏等 App 画像照常优先接管。
（MIT→GPL-3.0 兼容；移植署名见 `include/AutoMode.hpp` 顶部。）

**决策输入**：CPU 温度、电量、充电态、屏幕亮灭、时段（12 桶 = 6 时段 × 工作日/周末）。
时段边界同 ASB：睡 0–6 / 醒 6–9 / 上午 9–12 / 白天 12–17 / 傍晚 17–21 / 深夜 21–24。
温度读取移植了 ASB 的**尖峰守卫**（单 tick 跳升 ≥25℃ 且次高传感器不背书 → 判假尖峰，保持上次好值），
并读 **GPU 占用**（≥65% 计重载）让会话"持续重载"判定更准。
### auto 下的 FSM 主动调频（`fsm_enable`，默认开）

`auto` 模式额外移植了 ASB 的 **6 状态 FSM**（`AutoFsm.hpp`，源自 `asb_fsm.h`）：
`DEEP_IDLE / LIGHT_IDLE / MODERATE / HEAVY / SUSTAINED / GAMING`，每 2s 按 **GPU 占用 / CPU 负载 / 温度**
推进状态（GPU≥65%→GAMING、≥35% 或 load≥20→HEAVY、load≥14 或屏亮 GPU≥12→MODERATE、息屏→DEEP_IDLE），
升档 4s / 降档 10s 迟滞，最小驻留（HEAVY 8s / GAMING 25s / SUSTAINED 24s），SUSTAINED 57℃ 进 /49℃ 出
+180s 时间逃逸。每状态的频率上下限按 `lerp(floor, ceil, 状态级别)` 插值，再由学到的 **Smart alpha** 在
battery↔balanced 包络间混合后直接写各簇。频率界常量按 SM8650 参考授权，**运行时读本机
`cpuinfo_max_freq` 按 `界×本机max/参考max` 缩放**（移植 ASB `asb_bounds_scale`），所以**不写死型号**——
读频率表自适配每颗硅片（读不到则回退原值）。

**关键设计（不踩 Kin 旧坑）**：
- **仅 `auto` 启用**；`powersave/balance/performance/fast` 完全不变，仍是纯"governor 自主"。
- FSM **只改频率上下限、不改 governor** → 簇内仍由 walt 自主调频；且有 **DEEP_IDLE/LIGHT_IDLE 显式降档**
  （带降档迟滞与最小驻留），所以会正常降频，不会重现旧场景识别"粘高频"问题。
- **App 画像优先**：前台命中游戏等画像时 FSM 让位，画像退出再接管。
- **预热宽限（移植 ASB `balanced_warmup_grace`）**：亮屏会话开头 `fsm_warmup_grace_s`（默认 45s）内**抑制进 SUSTAINED 热降档**，
  避免"刚开游戏/应用的加载发热尖峰"把人卡一下；温度 ≥ `fsm_warmup_bypass_temp`（默认 60℃）则旁路、照常降频，不牺牲热安全。设 0 关闭。
- 想关掉回到"按 alpha 选整档"：`Function.AutoMode.fsm_enable = false`。

> **厂商压频保护**：保留检测、遥测和软钳降激进；主动抬高厂商频率上限的功能已停用，旧 `anticlamp_attack_enable` 配置不会重新启用它。

**自学习（每桶 `alpha` 0–1000 越大越省电 + 置信度，逐项对照 ASB 真源码 `asb_smart.h`）**：
每个亮屏会话结束按方向信号累加 `dt_alpha`（极热≥80℃ +80 / 热≥70℃ +40 / 高耗电 +30 /
持续偏热 +20 / 触热否决 +100 / 干净凉爽<55℃ −60），再 `× 学习率 80 / 1000` 落到该桶 `alpha`。
**时长权（25/50/100/125）× 信任权（CLEAN 100 / PARTIAL 40 / NOISY 15 / DIRTY 0）只累加进置信度
`eff_obs`，不影响 alpha 步长**（这点与 ASB 真源码一致）。桶置信 ~8 次干净长会话满信、按 **7/37 天衰减**；
低置信时决策向中性 500 收敛，并按 **精确桶→反周末同时段→同时段类→全局**（阈值 350）分级回退。

**置信度方差修正（把"样本计数"升级成"桶内一致性归一的真置信度"）**：原 `conf` 只看样本多少，
通勤/开会这种"同一时段忽闲忽忙"的高方差桶会**样本多但其实不可信**。现额外乘一个**一致性因子**：
跟踪每桶方向信号 `dt` 的平均绝对偏差（MAD），MAD=0→因子 1.0，MAD=`mad_scale`（默认 60）→因子 0.5，
越抖越低（满 3 会话后生效，之前等同原行为）。高方差桶置信被压低后会更早回退到稳定的邻桶/全局，
不再被"虚高的样本数"带偏。同时 `eff_obs` 从硬封顶 4000 改成**每会话按 `obs_retain_pct`（默认 97%）
衰减再累加的滑窗**——近期会话权重更高，也不再卡死在上限。设 `conf_var_enable=false` 回到纯样本计数。
状态存 `auto_state.txt`（`b<i>=alpha,obs,last_seen,base_cost,dt_mean,dt_mad,dt_samples`，`i=daypart*2+weekend`），
重启不丢（后段字段可选，旧 4/5 字段格式仍可读，向后兼容）。

**探索 + 奖励闭环（在纯利用之上补一层实测反馈）**：仅在已学会（conf≥350，有基线）的桶里，
以 `explore_pct%`（默认 4%）概率给该会话的 `alpha` 一个 ±`explore_offset`（默认 60）的小扰动，
方向**偏向性能侧**（系统结构性偏保守、性能侧原本无学习动力），且仅在不低电/不偏热/不充电时才往性能探。
会话结束用实测放电均流对比该桶基线算弱 reward：往省电探**真省到电**才奖励（`alpha +reward_step`），
没省到反而扣（白掉性能）；往性能探**电没多耗也不发热**才奖励（`alpha −reward_step`），否则扣。
扰动只作用在覆盖层之前，**温控/低电/夜间安全地板仍会把 alpha 钳回去 → 不破坏温控安全**；
设 `explore_enable=false` 即完全回到原来的纯利用行为。

**厂商压频检测 / anti-clamp（移植 ASB headroom + vendor-clamp，第 1 步）**：
读 `/sys/kernel/msm_performance/parameters/cpu_max_freq`（厂商内核强制的 CPU 上限）算 **headroom%** =
厂商上限 ÷ 本机硬件最大频。`<soft_pct`（默认 70）为**软钳**→按 ASB `soft_clamp` 语义"降激进"，
略上抬 alpha（更省电、不浪费电硬拼厂商热钳）；`<hard_pct`（默认 45）且温度不高 → 判**疑似厂商压频**
（非热因压频，正是 anti-clamp 的目标场景），计入滚动 1h 计数。这些都写进 `auto_status.txt`
（`headroom / headroom_valid / soft_clamp / hard_clamp / vendor_clamp / vendor_clamp_1h`）供 App / 诊断观察。

> ⚠ **8 Elite Gen5（SM8850）重要提醒**：ASB 源码自己注明——这颗 SoC 上 `msm_performance` 信号**经常是"死"的**
> （恒报满频 100%，只有真被钳时才偶尔报真值）。已**原样移植 ASB 的死信号检测**：连续 10 次满 100% 判为死信号 →
> 标 `headroom_valid=0`、**只遥测不参与决策**；之后连续 5 次合理读数再恢复。所以即使信号不可靠也不会乱调。
> 设 `anticlamp_backoff=0` 可让第 1 步退化成"纯遥测不调频"。

**主动抬频已移除**：不再写厂商频率上限，也不使用旧快照覆盖厂商后续限制。`anticlamp_attack` 和 `anticlamp_attack_total` 保留协议字段，恒为 0。

**覆盖层（安全优先，上抬 alpha 并与学习叠加，强制值同 ASB）**：
- 夜间安全：时段∈{睡,深夜,醒} + 息屏 + 未充电 → `alpha≥900`
- 低电（带迟滞：≤20% 且未充电触发，≥40% 或充电解除）→ `≥800`；临界 ≤10% 未充电 → `≥900`
- 热否决 ≥65℃ → `≥700`；50–65℃ 之间按 `alpha` 软提升曲线渐进偏省电

**可调阈值**（写在 `config.json` 的 `Function.AutoMode`，缺省即用 ASB 真值，无需重编译）：

```json
"Function": {
    "AutoMode": {
        "enable": "true",          // mode=auto 时是否真正接管(false 则退回 balance)
        "warm_temp": "50",         // 软提升起点(℃)
        "cool_temp": "55",         // 凉爽阈值: 峰温<此值且干净→学习偏均衡
        "hot_temp":  "70",         // 热信号(℃): 峰温≥此值→学习偏省电
        "vhot_temp": "80",         // 极热信号(℃)
        "veto_temp": "60",         // 热否决(℃): 强制 alpha≥700
        "low_batt":  "20",         // ≤此电量(%) 强制 alpha≥800
        "crit_batt": "10",         // ≤此电量(%) 强制 alpha≥900
        "night_start": "0",        // 夜间窗口: 00:00–06:00
        "night_end":   "6",
        "tick_sec":  "12",         // 亮屏决策周期(秒); 息屏自动放宽到 60s
        "learn_rate": "80",        // 学习率(/1000): dt_alpha×此值/1000(ASB LEARN_RATE_X1000=80)
        "drain_high": "14",        // 亮屏掉电率(%/h) ≥此值视为高耗电
        "cool_lean": "",           // "performance"=凉爽且低 alpha 时可冲高; 空=最高 balance
        "thermal_path": "",        // 可选: 指定 CPU 温度节点; 空=自动探测 thermal_zone

        // ── 会话方向信号 dt 系数(可配置, 默认=ASB 真源码值; 调省/调激进的旋钮) ──
        "dt_vhot": "80",           // 峰温≥vhot_temp(极热) → alpha +80
        "dt_hot": "40",            // 峰温≥hot_temp(热)    → +40
        "dt_drainy": "30",         // 高耗电(掉电率≥drain_high 或均流≥1200mA) → +30
        "dt_sustained": "20",      // 持续重载 → +20
        "dt_thermal_hit": "100",   // 触热否决(峰温≥veto_temp) → +100
        "dt_clean_cool": "-60",    // 干净凉爽会话 → -60(唯一负向项, 想更激进可调更负)

        // ── 探索 + 奖励闭环(补"纯利用零探索/无奖励"; 想完全回旧行为设 explore_enable=false) ──
        "explore_enable": "true",  // 仅在已学会(conf≥350)的桶里小概率试探更优 alpha
        "explore_pct": "4",        // ε: 已门控会话里触发探索的百分比(0..30)
        "explore_offset": "60",    // 单次探索的 alpha 扰动幅度(0..200; 不会越过温控/低电安全地板)
        "reward_step": "8",        // 探索被实测验证后给该桶的永久微调步长(0..50)

        // ── 厂商压频检测 / anti-clamp(第1步: 检测+遥测+软钳降激进; 信号死则自动旁路) ──
        "anticlamp_enable": "true",   // 读 headroom 做厂商压频检测; false=完全关闭(字段全 -1/0)
        "anticlamp_soft_pct": "70",   // headroom<此%=软钳(降激进, 略上抬 alpha)
        "anticlamp_hard_pct": "45",   // headroom<此% 且温度不高=疑似厂商压频(计 1h 计数)
        "anticlamp_backoff": "40",    // 软钳时上抬 alpha 的幅度(设 0=只遥测、不调频)

        // 旧 anticlamp_attack_* 字段仅兼容读取，不执行主动抬频。

        // ── 置信度方差修正 + eff_obs 滑窗 ──
        "conf_var_enable": "true",    // 置信度乘"桶内一致性因子"; false=回到纯样本计数(原行为)
        "mad_scale": "60",            // 一致性尺度: 桶内 dt 的 MAD=此值时置信×0.5
        "obs_retain_pct": "97"        // eff_obs 每会话保留比例(<100=滑窗衰减, 不再永久封顶)
    }
}
```

> **学习重置 / 导出诊断**：往 `/sdcard/Android/Kin/auto_control.txt` 写一行命令即可（守护 ~5s 内消费并删除该文件）：
> `reset`（全部 12 桶清零回中性）、`reset_bucket=N`（只清第 N 桶, N=0..11）、`export`（把每桶 alpha/conf/基线
> 导出到 `auto_export.txt` 便于核对学习效果）。换班 / 差旅作息突变时用 `reset` 可快速纠偏，无需重装。

> **致谢 / 许可**：自动模式算法源自 AutoSystemBoost（MIT, © 2026 Dima Krylov）。
> 本仓库为 GPL-3.0，MIT 与 GPL-3.0 兼容；已按 MIT 要求在 `include/AutoMode.hpp` 保留版权与许可声明。

实时状态写入 `auto_status.txt`（`resolved` 当前档 / `bucket` 时段桶 / `alpha` / `temp` / `batt` …），
方便前端或调试查看。默认偏保守：`cool_lean` 为空时 `auto` 不会主动冲 `performance`，
想让它在凉爽时段冲高频，把 `cool_lean` 设为 `"performance"` 即可。

---

## CPU 频率映射（8 Elite Gen 5 / 6+2）

| 集群 | 核心数 | 类型 | 配置变量 | 频率范围 |
|------|--------|------|----------|----------|
| **c0** | 6 | Pegasus 小核 | `freq_min_c0` / `freq_max_c0` | 约 300MHz–3.62GHz |
| **c1** | 2 | Phoenix-Prime 大核 | `freq_min_c1` / `freq_max_c1` | 约 800MHz–4.6GHz |

> 实际档位以你机器的 `/sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies` 为准。

---

## 调频模型（无 Scenes，governor 自主）

### 设计理念

早先用 Scenes（Standby / HeavyLoad / Touch / None）覆盖频率上下限，但实测发现：

1. **CPU 几乎不降频** —— scene 频繁切换重写 `scaling_max_freq`，governor 还没来得及自然回落就被改了
2. **配置复杂** —— 5 个 scene × 4 个簇 × min/max/gov = 60 个字段要管理
3. **响应过激** —— Touch 每秒触发多次，HeavyLoad 一旦触发就 boost，反而抖动费电

参考 [yumi](https://github.com/imacte/yumi) 和 [MW_CpuTurboScheduler](https://github.com/MoWei-2077/MW_CpuTurboScheduler)：

> **Kin 只设上下限和 governor 参数；具体频率响应交给 governor 自己。**

### 三个关键 governor 参数

调度器（walt / schedutil / hmbird）支持下面三类参数，写到 `gov_cN.params` 里：

| 参数 | 单位 | 含义 | 影响 |
|------|------|------|------|
| `up_rate_limit_us` | 微秒 | 升频最小间隔 | **小**=升频快（响应好但费电），**大**=升频慢 |
| `down_rate_limit_us` | 微秒 | 降频最小间隔 | **小**=降频快（省电），**大**=降频慢 |
| `hispeed_load` | 百分比 | 触发 hispeed 的负载阈值 | **小**=容易升频，**大**=不易升频 |

### 各模式默认参数（参考值）

| mode | up_rate (升频) | down_rate (降频) | hispeed_load | 取向 |
|------|----------|--------------|--------------|------|
| **powersave** | 20000 μs（慢） | 4000 μs（快） | 95% | **极度省电**，降频积极 |
| **balance** | 10000 μs | 4000 μs（快） | 90% | **降频积极**，升频中等 |
| **performance** | 5000 μs（快） | 8000 μs | 85% | 升频积极，降频适中 |
| **fast** | 2000 μs（极快） | 16000 μs（慢） | hmbird 内置 | 高频粘性，**风驰** |

**关键经验值**：
- `down_rate_limit_us` 在 powersave/balance/视频/阅读/音乐模式都设 ≤ 4000，**这是 CPU 下得来的核心**
- `up_rate_limit_us` 在游戏/性能模式设 ≤ 5000 才不卡
- `hispeed_load` 反向：90+ = 不容易升频（省电），80-85 = 容易升频（流畅）

### 如果还是觉得不降频

1. 看你内核当前 governor 实际支持的参数：
   ```bash
   ls /sys/devices/system/cpu/cpufreq/policy0/walt/    # walt
   ls /sys/devices/system/cpu/cpufreq/policy0/schedutil/  # schedutil
   ```
2. 验证写入是否生效：
   ```bash
   cat /sys/devices/system/cpu/cpufreq/policy0/walt/down_rate_limit_us
   ```
3. 实测当前频率：
   ```bash
   cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
   ```

---

## 配置文件结构

```json
{
    "meta": { ... },           // 基本信息
    "Policy": { ... },         // CPU 集群索引
    "Function": { ... },       // 功能开关（已大幅精简）
    "package_blacklist": [...],// 黑名单 App
    "models": [ ... ]          // 各场景频率配置
}
```

### 1) meta

```json
"meta": {
    "name": "Kin-8Elite-Gen5",
    "version": "45",
    "author": "你的名字",
    "loglevel": "INFO",
    "soc": "SM8850",
    "arch": "6 Pegasus + 2 Phoenix-Prime"
}
```

### 2) Policy（CPU 集群索引）

```json
"Policy": { "c0": "0", "c1": "6", "c2": "-1", "c3": "-1" }
```

**怎么查？**
```bash
ls /sys/devices/system/cpu/cpufreq/
# 8 Elite Gen 5: policy0 + policy6 → c0=0, c1=6
# 普通 8 Gen 3:   policy0 + policy4 + policy7 → c0=0, c1=4, c2=7
```

### 3) Function（精简到只剩 4 项）

```json
"Function": {
    "Cpuset":       { "enable": "true", ... },
    "LaunchBoost":  { "enable": "false" },
    "OfficialMode": { "enable": "false" },
    "Scheduler":    { "enable": "true", "sched_energy_aware": "true", ... }
}
```

> **配置精简**：以下模块底层已默认开启，**配置里直接省略**：
> - `SceneDetect` — 场景识别默认开
> - `AppProfile` — 应用画像默认开
> - `GpuFreq` — GPU 频率守护默认开
> - `NetlinkScreen` — Netlink 屏幕监听默认开
>
> 想关掉或调参，再显式声明覆盖即可：
> ```json
> "Function": {
>     "SceneDetect": { "enable": "false" }   // 显式关闭
> }
> ```

### 4) package_blacklist

```json
"package_blacklist": [
    "com.android.systemui",
    "com.android.launcher*",   // 通配符
    "com.baidu.input*"
]
```

### 5) models（核心）

```json
{
    "model_name": "balance",
    "mode": "balance",                                // 对应 mode.txt
    "is_game": false,
    "packages": [],                                    // 空数组 = 基础模型，不参与 App 匹配
    "freq_min_c0": "400000",  "freq_max_c0": "2800000",
    "freq_min_c1": "1000000", "freq_max_c1": "3400000",
    "gpu_min_mhz": "0",       "gpu_max_mhz": "900",
    "CoreOnline": {
        "Core0": "1", "Core1": "1", "Core2": "1", "Core3": "1",
        "Core4": "1", "Core5": "1", "Core6": "1", "Core7": "0"
    },
    "gov_c0": {
        "governor": "walt",
        "params": {
            "hispeed_freq":      "1600000",
            "hispeed_load":      "90",
            "up_rate_limit_us":  "10000",
            "down_rate_limit_us": "4000"
        }
    },
    "gov_c1": {
        "governor": "walt",
        "params": {
            "hispeed_freq":      "2100000",
            "hispeed_load":      "90",
            "up_rate_limit_us":  "10000",
            "down_rate_limit_us": "4000"
        }
    }
}
```

> 已移除 `Scenes` 字段。如果你的旧 config 还有 `Scenes` 节点，Kin 会**忽略**它（不报错）。

### 风驰模式（fast）+ 游戏模型

`fast` 和 `game_heavy` 模型都用 `hmbird` 调速器，响应比 `walt` 更快：

```json
"gov_c0": {
    "governor": "hmbird",
    "params": {
        "up_rate_limit_us":  "2000",
        "down_rate_limit_us": "16000"
    }
}
```

游戏模型把 `down_rate_limit_us` 调大（16000μs）保持高频粘性，避免战斗中频率震荡。

---

## 常见问题

### Q: 游戏感觉性能不够？
- 直接把游戏包名放进 `game_heavy.packages`，AppProfile 会按 App 一次性把频率拉满
- 也可以新建一个自定义 model（参考 game_heavy），调大 `freq_min_c1`（拉高大核底线）

### Q: CPU 怎么都不降频？
三个常见原因：
1. **基础模型没设 `down_rate_limit_us`** → governor 按内核默认（通常太大）→ CPU 粘高频
2. **AppProfile 退出未还原 SchedParam** → 游戏的 `down_rate_limit_us=16000` 一直生效
3. **scene 频繁切换重写 `scaling_max_freq`** → governor 还没来得及自然回落就被改了

Kin 的应对：
1. 所有模板模型都明确设 `down_rate_limit_us`（powersave/balance=4000，performance=8000）
2. `applyBaseMode()` 会重新写基础 SchedParam（游戏退出后 rate_limit 一定还原）
3. 不走场景切换路径，CPU 频率上下限不再被频繁重写

### Q: 配置改了没生效？
1. 看日志：`cat /sdcard/Android/Kin/log.txt`
2. JSON 是否合法（推荐用 `python3 -m json.tool config.json` 验证）
3. `loglevel` 改 `DEBUG` 看更详细的写入路径
4. 验证 governor 参数实际生效：`cat /sys/devices/system/cpu/cpufreq/policy0/walt/down_rate_limit_us`

### Q: 写 `"true"` 还是 `true`？
- 两种都行，Kin 内部容忍。**推荐统一写 `"true"`**（key 和 value 都加引号），少记一个规则

### Q: 哪些 App 算游戏？
- `game_heavy.packages` 已内置 25 款主流大型手游（王者、和平、原神、星铁、绝区零、明日方舟等）
- 加新游戏直接往 `packages` 数组里加包名

---

## 实现要点

- **无场景检测线程** —— 不启动 `sceneTickTask` / `screenStateTask` / `loadSamplingTask` / `touchDetectTask`，进程更轻,频率上下限不被频繁重写
- **模板模型都显式设 rate_limit** —— powersave/balance/video/reading/music 的 `down_rate_limit_us` ≤ 4000μs(CPU 真正能降频的关键)；游戏/性能模式 `up_rate_limit_us` ≤ 5000μs 保证响应；风驰 fast 用 `down_rate_limit_us=16000` 维持高频粘性
- **退出 AppProfile 必还原** —— `applyBaseMode()` 重写基础 mode 的 SchedParam,杜绝游戏的 `down_rate_limit_us=16000` 残留导致"回桌面不降频"
- **频率响应交给 governor** —— 用 `up_rate_limit_us` / `down_rate_limit_us` / `hispeed_load` 控制升降频节奏,governor 有足够时间自然回落
- **SoC 适配层（`include/Soc.hpp`）** —— 所有机型/平台相关的 sysfs 路径（measured_fps / GPU / 电流 / 背光 / headroom 节点 / 温度传感器关键字）与参考频率（`REF_HWMAX_*`）集中在一个文件。**适配 MTK / 其它骁龙只需改这一处**：路径都是"候选列表"，运行时逐个探测、命中即用，多写几个不同平台的候选是安全的（读不到自动跳过）。算法逻辑（AutoMode/AutoFsm/AutoFas）不再内嵌任何硬编码路径。

> 逐条改动历史见 [changelog.md](./changelog.md)。

---

## 协议

GPL-3.0
