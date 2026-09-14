# 内核接口 boost 手段调研（powerd v4 / Kin 智能档）

> 数据来源：两个研究 workflow 共 85 条 agent 结果 —— wf_d8960494（fas-rs / AutoSystemBoost 代码级调研）+ wf_63a14806（Linux 内核 / AOSP 文档调研），含 60 余条 claim 与 20 条对抗性验证。
> 来源质量标注：**primary**（内核文档 / AOSP 源码 / 项目源码 / LKML / 厂商 init 脚本）、**blog**、**secondary**、**unreliable**。
> 本文档聚焦「能落到 shell / Kin 现有代码上的结论」，并列出现有实现中三处已确认的 UCLAMP 误用。

---

## 目录

1. [三处关键纠错（针对现有代码，务必先读）](#一三处关键纠错)
2. [可直接 shell 实现的 boost 手段](#二可直接-shell-实现的-boost-手段)
3. [measured_fps 帧率闭环](#三measured_fps-帧率闭环)
4. [PSI 卡顿检测](#四psi-卡顿检测)
5. [thermal trend 热趋势](#五thermal-trend-热趋势)
6. [UCLAMP / EAS 正确用法](#六uclamp--eas-正确用法)
7. [必须 C 代码才能做的部分](#七必须-c-代码才能做的部分)
8. [被推翻或存疑的说法](#八被推翻或存疑的说法)
9. [对 Kin / powerd 的落地建议](#九对-kin--powerd-的落地建议)

---

## 一、三处关键纠错

这三点直接影响 `powerd.sh` 与 `scheduler.hpp` 现有 UCLAMP 逻辑的正确性，是本轮要修的核心。

### 纠错 1：`sched_util_clamp_min` 不是全局 boost（primary，内核文档）

`/proc/sys/kernel/sched_util_clamp_min` 的语义是**「系统允许的 UCLAMP_MIN 上限」**，默认 1024。

- 写 1024 ≠ 全局提速，只是「解除对 uclamp_min 请求的封顶」，本身不产生任何 boost。
- 写一个小值（如 128）**反而是限制**：任何任务（含 OEM 给 top-app 设的 per-task uclamp_min）请求的性能下限都会被钳到这个上限以下。
- 真正的全局 boost 只能靠 `sched_boost`（WALT 内核）或 cgroup 的 `cpu.uclamp.min`。

**现有代码问题**：
- `powerd.sh` 的 `set_uclamp()` sysctl 分支：`[ "$umin" -gt 0 ] && echo 1024 ... || echo 0`，把该节点当 boost 开关用。写 1024 无 boost 效果，写 0 会压掉全系统 uclamp_min 能力。
- `scheduler.hpp` 的 `applyPowerBoost()` sysctl 模式（mode 2）：卡顿档写 `power_boost_value`（默认 128）到该节点 —— 128 作为「允许上限」是在**限制**而非 boost，方向反了。

### 纠错 2：cgroup `cpu.uclamp.*` 单位是百分比 0–100，不是 0–1024（primary）

cgroup v2 的 `cpu.uclamp.min` / `cpu.uclamp.max` 接受的是**百分比整数 0–100**（或字面量 `max`），而 `/proc/sys/kernel/sched_util_clamp_*` 才是 0–1024 刻度。两者不能混用同一个数值。

**现有代码问题**：
- `powerd.sh` 的 `set_uclamp()` cgroup 分支：直接把 `UCLAMP_MAX_L1/L2/L3`（768/512/256，0–1024 刻度）写进 `cpu.uclamp.max`。内核会因超范围拒绝（`-ERANGE`）或钳到 100，导致限频静默失效。
- `scheduler.hpp` 的 `applyPowerBoost()` cgroup 模式（mode 1）：把 `power_boost_value`（128，0–1024 刻度）写进 `/sys/fs/cgroup/cpu.uclamp.min`，同样超范围。boost 静默失败。
- 对照：`scheduler.hpp` 的 `applyTopAppUclampMin()` 是**对的** —— 它明确按百分比 0–100 传值，注释也写清了。可见作者知道 cgroup 是百分比，`applyPowerBoost` 这处是疏漏。

修法：写 cgroup 前把 0–1024 换算成百分比 `pct = round(v * 100 / 1024)`。

### 纠错 3：`sched_util_clamp_max` 不能可靠限频（primary + blog 一致）

`scheduler.hpp:1355` 注释把 `sched_util_clamp_max` 说成「允许下限」—— 语义标反了，它是「允许的 UCLAMP_MAX **上限**」。不过该行的结论（cap 一律走 cpufreq）是对的，只需修正描述。

更重要的是 **uclamp_max 在 max 聚合下基本挡不住频率**：runqueue 按所有 runnable 任务取 max 聚合，只要有一个未封顶的任务入队，cap 就被绕过。且未含 2023-08 上游 DVFS headroom 修复的内核（大量 Android 5.10/5.15 分支），uclamp_max 实际有效范围是 **0–819** 而非 0–1024，写值与实际频率非线性。

**结论**：`powerd.sh` 用 `UCLAMP_MAX_L1/L2/L3` 做限频是不可靠路径。限频主力必须是 `scaling_max_freq`（apply_level 里已经在做），uclamp_max 只能算辅助，别指望它省电。

---

## 二、可直接 shell 实现的 boost 手段

### 2.1 cpuset 场景分组（primary，主力手段，零常态开销）

| 节点 | 典型值 | 作用 |
|---|---|---|
| `/dev/cpuset/top-app/cpus` | `0-7`（全核） | 前台顶层应用铺满大小核 |
| `/dev/cpuset/foreground/cpus` | `0-2,4-7` | 可感知非顶层应用 |
| `/dev/cpuset/background/cpus` | `0-1` | 后台压到小核 |
| `/dev/cpuset/system-background/cpus` | `0-3` | 后台系统进程限小核 |

三条纪律（primary）：
- **路径必须探测**：libprocessgroup 用 `CgroupMap::FindController("cpuset")` 动态定位，脚本要同时探 `/dev/cpuset` 与 `/sys/fs/cgroup/cpuset`。
- **只用官方子组名**：`""` / `foreground` / `background` / `system-background` / `top-app` / `restricted` / `foreground_window`，自建子组会让框架侧 `get_sched_policy_from_group` 返回 `-1/ERANGE`。
- **cpu 控制器优先于 cpuset**：`get_sched_policy` 查找顺序是 cpu 控制器优先、cpuset 兜底。只改 cpuset 而 cpu cgroup 不动，框架读回策略可能与意图不符。
- cpus 列表**可以非连续**（如 `0-2,4-7`），厂商 primary 脚本就这么写，之前「必须连续」的说法是错的。

### 2.2 启动期激进、稳态回落（primary，小米 sm6225 脚本模式）

early-init 全局 `schedtune.boost 50 + prefer_idle 1` 抢启动速度 → `boot_completed` 后重置为 `0/0`，只留 top-app `prefer_idle 1 / boost 1`。这个模式能规避「常态高 boost 烧电」，值得直接借鉴。Kin 的 `warmup_grace` / powerd 的 `WARMUP_GRACE` 已是同类思路。

### 2.3 限频与热调控（primary，shell 完全够用）

- `cpuX/cpufreq/scaling_max_freq`：LineageOS/Sony thermmanager 的降频手段就是纯 sysfs 写最大频率，生产级验证。**这是限频的可靠主力。**
- `/sys/class/thermal/thermal_zoneX/temp`：毫摄氏度，除 1000。
- `trip_point_N_temp` / `_hyst`：部分驱动可写，能让 shell 侧降温抢在内核 trip 之前。

### 2.4 低开销采样通道（blog，有量化）

- 直读 `/proc/stat`、`/proc/pressure/*`、`/proc/meminfo` 比 `dumpsys` 快 500–1000 倍（`cat /proc/meminfo` <5ms vs `dumpsys meminfo` 5–15s）。
- **严禁在闭环里调 `dumpsys` / binder**：每 17s 一次 `dumpsys meminfo` 会占满 system_server、拖慢 SurfaceFlinger、直接掉帧 —— 采集动作本身污染被测性能。
- 分层采样建议：廉价通道 15–30s，重型通道 ≥2 分钟。

> powerd.sh 的 `read_top_app` 已优先走 cpuset top-app / top，dumpsys 仅作背光探测兜底，方向正确。

---

## 三、measured_fps 帧率闭环

### 3.1 帧时间采样：shell 不可行（primary，3 条独立验证 conf=high）

fas-rs 通过 **inline hook**（v1.7.0 起 Zygisk 注入 hook libgui）取 frametime，本质是向进程内存打补丁。shell 只能读写 sysfs/proc，**做不了进程内 hook**。要帧率只能依赖已有的 fas-rs，或退回占用率闭环（下）。

### 3.2 占用率闭环：shell 可完整复刻（primary，本轮最有价值的发现）

fas-rs 在 2024-11-26 把 target_fps 自动微调**从帧时间方差改成了 CPU 占用率监测**：

```
每 1 秒采样：usage = 所有 CPU policy 中的最高单核占用率（usage_max）
usage <= 40%  → target_fps_offset -= 0.1   （有余量，降目标省电）
usage >= 50%  → target_fps_offset += 0.1   （繁忙，抬回目标）
40% < usage < 50% → 滞回死区，不动作
target_fps_offset 钳位 (-5.0, 0.0)
```

纯读 `/proc/stat` 即可，无内核驱动、无 sysfs 写入。**这是 fas-rs 现役算法**（替换了旧方差法），说明真实设备上够用。要点：用**最忙核**而非平均，滞回死区不能省。

> Kin 的 FSM 已是「占用率 + 电流 + 温度」多信号状态机，比单占用率更强；此结论主要给 powerd 参考。

### 3.3 纯比例控制器（primary，逐 commit 核实）

fas-rs 2024-11-09 commit e885805 **删除了 PID 的 I、D 项，退化为纯比例**：
`error_p = (current_frametime - target_frametime) * kp`，`kp` 默认 0.000_3，钳制 0.000_1 ~ 0.000_8。setpoint 偏置 `target_fps = 配置值 - 0.5`（增稳）。启示：帧率闭环不需要完整 PID，纯比例 + 滞回 + 死区足矣。

---

## 四、PSI 卡顿检测

### 4.1 接口（primary，内核 accounting/psi.rst）

`/proc/pressure/{cpu,memory,io}`，格式：
```
some avg10=0.00 avg60=0.00 avg300=0.00 total=0
full avg10=0.00 avg60=0.00 avg300=0.00 total=0
```
- `some` = 至少部分任务被阻塞的时间占比；`full` = 所有非空闲任务同时被阻塞（thrashing）。
- **cpu 只有 `some` 行**（早期内核无 full）。
- cgroup v2 每子目录有 `cpu.pressure` 等 → 可做前台应用粒度检测。

### 4.2 可用阈值（三组独立 primary）

| 来源 | some | full | 窗口 |
|---|---|---|---|
| AOSP lmkd | 70ms（高性能）/ 200ms（低内存） | 700ms | — |
| LPC 2019 | 10% | 7% | 1 秒 |
| 第三方 LMK 模块 | 50/70/100 ms | 400/500/700 ms | — |

powerd v4 起点：1s 窗口，cpu `some ≥ 10%` 轻压力，mem/io `full ≥ 7%` 真卡顿。

> Kin 的 `power_boost_jank_psi = 20`（cpu some avg10 ≥ 20%）比 10% 更保守，只在明显卡顿才 boost，合理。

### 4.3 开销与误报

- 内核侧开销**自适应**：只在被监视 stall 实际发生时才高频聚合，消退即回落。适合常驻。
- PSI 比 vmpressure 误报少约 **10 倍**。
- **不要用单阈值**：qos-android 对 `/proc/pressure/cpu` 做「速度(velocity)+积分(integral)」追踪。单阈值在负载抖动时会频繁误触发。Kin 用「PSI 或 (busy 高 且 load 高)」双确认，已比单阈值稳。
- **存在性即前提**：`/proc/pressure` 缺失时应降级。Kin 的 `cpuPsi < 0` 回退到 busy+load 已做对。

---

## 五、thermal trend 热趋势

### 5.1 现状（primary，kernel 6.6）

- 内核热趋势只有三态 `THERMAL_TREND_STABLE/RAISING/DROPPING`，由驱动 `get_trend()`（C 代码）算，**用户态拿不到**，只能自己轮询 temp 推算。
- `step_wise` governor 用趋势做方向而非预测；实测是带回滞的离散跳变。
- **两个生产项目（Sony/LineageOS thermmanager、fas-rs）都只用静态阈值 + 迟滞，无趋势预测**。→ 趋势预测是可选增强，不是必需。

### 5.2 shell 可行思路

1. 轮询 `thermal_zoneX/temp`，保留最近 N 样本，一阶差分（ΔT/Δt）判 RAISING/DROPPING/STABLE + 死区，等价内核 `get_trend` 的三态粒度。
2. `sustainable_power` 语义可借用：功耗高于它温度持续升、低于它降 —— 用「当前负载是否让温度持续爬升」代替功耗建模。

> Kin/powerd 的 FSM 温度迟滞（enter/exit + EMA 平滑）已覆盖，无需引入趋势预测。

---

## 六、UCLAMP / EAS 正确用法

### 6.1 UCLAMP 设计意图（primary，内核文档）

- UCLAMP 是**请求/提示，不是硬约束**。UCLAMP_MIN=boost，UCLAMP_MAX=cap；不修改任务真实 PELT util，只在决策点用 clamped 值。
- **只有 EAS/CAS 会消费 uclamp** —— 只在异构大小核系统上有意义。
- 用 `UCLAMP_MIN=1024` 可克服 PELT 爬升延迟（任务一开跑就见最高性能点）。
- 文档明确建议「避免无反馈回路的静态配置」—— 支持 Kin/powerd 做闭环而非写死。

### 6.2 四个必须避开的误用（primary）

1. `sched_util_clamp_min` 是「允许范围上限」不是全局 boost（见纠错 1）。
2. uclamp_max 在 max 聚合下挡不住频率（见纠错 3）。
3. DVFS headroom 1.25 倍：未含 2023 修复的内核上 uclamp_max 实际范围 0–819，写值与频率非线性，必须实测标定。
4. 严重封顶会破坏 over-utilization 信号，让 EAS 一直不退出。

### 6.3 EAS 生效前提（primary，决定所有调节是否有效）

1. **只支持 schedutil governor**，否则内核打印 `Disabling EAS, schedutil is mandatory`。
2. 需要频率不变 + CPU 不变的 PELT 信号。
3. **任一 CPU 超 80% 容量就退出 EAS**（`capacity_margin=1280`）。→ **重载游戏场景下 EAS 大概率已不工作，此时 uclamp 的选核效果消失、只剩频率影响。**
4. 只在异构拓扑运行，不支持 SMT。

> Kin 的 `CPU_HEAVY_ENTER=80` 正好对齐这个 0.8 容量拐点，注释里也引了三处一致依据，判断准确。

### 6.4 高通 vs 通用内核

| | 高通/Android common | 上游主线 |
|---|---|---|
| 特性 | SchedTune、WALT | uclamp + util_est |
| 节点 | `/dev/stune/*/schedtune.boost`、`sched_boost`、`cpu_boost/*` | `cpu.uclamp.*`、`sched_util_clamp_*` |
| 主线 | 不在 | v5.3 起 |

- `sched_boost` 只在 `CONFIG_SCHED_WALT` 存在；只能 0↔非0 切换，否则 `-EINVAL`（先归零再换档）。
- 高通 sched_boost 强依赖 `top-app` stune 组存在，缺失会报 `Could not find top-app`（有 msm8996 整体回退 20 个提交的实例）。
- Dynamic SchedTune Boost 每组 5 槽位，**不用时必须 reset，否则永久保持提升** —— 若走这条路必须有 watchdog。

---

## 七、必须 C 代码才能做的部分

| 能力 | 为什么 shell 做不到 | 来源 |
|---|---|---|
| frametime 采样 | inline hook / Zygisk 注入 libgui，需向进程内存打补丁 | primary ×3 |
| PSI 事件等待 | 触发器可 shell `write`，但事件到达靠 `poll()`，shell 无 poll 原语；需几十行 C helper | primary ×2 |
| 逐核硬件周期计数 | `perf_event_open` 是系统调用，且需 `sched_setaffinity` 钉核 | primary |
| 内核算好的 thermal trend | `get_trend()` 是驱动回调，不导出用户态 | primary |
| per-task uclamp | `sched_setattr` 是系统调用；**但** `uclampset` 命令行 + cgroup 路径可绕过，不算硬边界 | primary |

---

## 八、被推翻或存疑的说法

### 8.1 已推翻（refuted，conf=high）

- **`/dev/fas_rs/mode` 不是字符设备、不需要内核驱动**。源码 `create_node` 就是 `fs::write`，全仓无 `mknod`/`register_chrdev`，构建只需 NDK + Rust。它是守护进程用 root 在 /dev 下建的普通文本文件，shell 直接写即可（前提 fas-rs daemon 在运行）。历史 v1/v2 曾需内核模块，当前版本已全用户态。
- fas-rs 控制面不止 `mode` 一个节点：还有 `games.toml`（sdcard，可 shell 读写 per-app target_fps/margin_fps）、`/dev/fas_rs/extensions` 等。

### 8.2 存疑（不要当结论）

- **AutoSystemBoost 全部内容**（unreliable，目录页、未验证）：六态 FSM 阈值、Anti-Clamp 四阶段、Thermal Debt、所有效果数字（−40%/−64% 等）。方向可参考，**数字必须自行实测**。
- `user_space` thermal governor 通过 uevent 上报温度免轮询（blog 单源）：机制可能存在，实机验证前不作唯一热信号通道。
- sum 聚合 uclamp、`util - util_est` boost：均未合入主线，设计必须按 max 聚合语义。

---

## 九、对 Kin / powerd 的落地建议

### 已确认要改（本轮）

1. **powerd.sh `set_uclamp()`**：
   - cgroup 分支：写 `cpu.uclamp.min/max` 前把 0–1024 换算为百分比 0–100。
   - sysctl 分支：停止把 `sched_util_clamp_min` 当 boost 开关（写 1024/0）。boost 交给 `sched_boost`；该节点保持默认，不写。
2. **scheduler.hpp `applyPowerBoost()`**：
   - cgroup 模式（mode 1）：`power_boost_value` 写入 `cpu.uclamp.min` 前换算为百分比。
   - sysctl 模式（mode 2）：停止把 `power_boost_value` 写进 `sched_util_clamp_min`；boost 依赖 `sched_boost`。
   - 修正 `:1355` 注释：`sched_util_clamp_max` 是「允许的 UCLAMP_MAX 上限」不是「允许下限」。

### 第一梯队（高收益低风险，证据充分）

- cpuset 场景分组（零常态开销）
- 占用率驱动预算闭环（fas-rs 现役算法，powerd 可直接抄）
- PSI 轮询做卡顿降级（Kin 已实现，加 velocity/连续 N 次可更稳）
- 静态阈值 + 迟滞温控（两生产项目背书，Kin/powerd 已做）
- 启动期激进、稳态回落（Kin warmup_grace / powerd WARMUP_GRACE 已做）

### 明确不建议

- 不要在 shell 复刻 measured_fps 帧时间闭环（采样端硬边界）
- 不要落地 CPCS 关键路径分簇（作者自述无完整算法、非游戏不适用）
- 不要在闭环调 dumpsys/binder（污染测量、掉帧）
- 不要动 GPU devfreq（本轮无 primary 证据，厂商脚本无相关写入）
- 不要指望 uclamp_max 省电限频（max 聚合绕过）

---

*本文档由研究 workflow 结果综合生成，结论按来源质量分级。primary 级可直接采用，blog/unreliable 级需实机验证。*
