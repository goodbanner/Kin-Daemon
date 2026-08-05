# ASB V57「network for gamers」对照 Kin(CTS-Daemon)移植研究

> 研究性文档,不改运行代码。方法:对照上游 **ASB V57 真源码**(`src/asb_*.{h,c}`,约 1.2 万行)+ **V57 打包模块**(shell 脚本 + 剥符号 ELF `bin/asb` + 分档配置)+ **ELF 字符串** 三份素材,逐条 grep/read 取证,再用「唱反调」方式对每条候选做适用性/忠实度对抗核验(37 个子代理:6 维度深挖 + 30 条对抗核验 + 1 汇总)。
>
> 目标机:OnePlus V Pro · SM8850 / 骁龙 8 至尊版 8 Elite Gen 5(8e5)。

---

## 1. 一句话结论

V57 是一次**纯网络 + 内存侧**的省电/游戏低延迟发布(CPU 调度核心几乎没动),把「每档 TCP 行为差异化」做成卖点:省电档让连接躺平(RTO 120s、keepalive 5h、关 thin),游戏/性能档抢低延迟(RTO 8s、开 tcp_thin、BBR)。官方现场数据:待机耗电 1.89→0.66 %/h、活动 10.1→6.4 %/h、待机峰温 63→48℃、厂商钳位「写战」占 tick 59%→24%。

对 Kin 最值得做的,按性价比:

1. **SysTune 网络增强(headline)** —— 每档补 `tcp_thin_*` + `tcp_rto_max` + keepalive 三元组 + 动态 `tcp_mem` + BBR/pacing + 一组延迟/缓冲 sysctl。全落在 `SysTweaks.hpp`,全 only-if-exists 纯 sysctl、重启自还原、零设计冲突。
2. **swappiness 镜像 OxygenOS 私有属性** —— 正好命中 OnePlus 目标机。
3. **一处真实漂移待决**:V57 **下调**了 battery/balanced 的 FSM 频率天花板(`asb_fsm_bounds.generated.h`),Kin 的 `AutoFsm.hpp` 仍是旧的更高值 —— 见 §6,需你拍板是否同步。

**明确不碰**:PLB(节点名是 ASB 杜撰、8e5 无此节点)、安全硬化 sysctl(超出调度器职责)、`net_conservative`(上游死字段)、smart 时段种子(与 Kin「冷启动中性 500」设计冲突)、anti-clamp 写战三件(Kin 本就不循环重写,已更省)、`device_bounds_override`(Kin 运行时缩放已等价/更优)、vendor overlay / 相机 / 音频 / bootloop(改 vendor 分区,范围外)。

---

## 2. V57 变化总览

主题 **"network for gamers"**。四个变更桶:

| 桶 | 内容 | 对 Kin |
|---|---|---|
| (a) Ace6 bootloop 修复 | 删 49 个静态 vendor 文件、设备分级 boot 熔断(参考机 3-strike / 其它 1-strike)、`/data/adb/asb/vendor_overlay_blocked` 标记 | **范围外**(Kin 无 overlay 机制) |
| (b) 场景感知 TCP(本文核心) | 每档 thin/RTO/mem/BBR 差异化 + smart 跟日模式 | **主要可移植面** |
| (c) `device_bounds_override` | OP15 开、其它关,WebUI 开关移除(`asb_config.h:163` 默认 0) | 已具备更优机制 |
| (d) swappiness 镜像 sysprop | `sys.sysctl.swappiness`/`sys.mem.swappiness_on_launcher`/`_on_start`,仅当已存在才 resetprop;规避开机期 zram resize | **可移植(命中目标机)** |

素材路径(供复现):
- 上游真源码:`asb_governor.c`(5819 行,主守护)/ `asb_smart.h`·`asb_smart_defs.h`·`asb_learner.h`(学习)/ `asb_fsm.h`·`asb_fsm_bounds.generated.h`(FSM)/ `asb_metrics.h`(传感)/ `asb_config.h`(配置)/ `asb_writer.h`(sysfs 写)。
- 打包模块:`profiles/{battery,balanced,performance}.sh`(每档 `NET_*`/`VM_*`)→ `runtime/asb_utils.sh`(映射 `_P_*`)→ `service.sh apply_net()/apply_vm()`(落 sysctl)。
- 网络的智能选档在 shell(`asb_reconcile.sh`),不在 C 守护。

---

## 3. 逐维度分析

### 维度 1 —— 每档网络 sysctl 矩阵(headline)

V57「network for gamers」**全部在 shell 实现**(`service.sh apply_net()` L559-692,约 70 条 sysctl;ELF 字符串里没有这些常量,证明纯 shell/sysfs)。三档差异化真值:

| 字段 | battery | balanced | performance | Kin 现状 |
|---|---|---|---|---|
| swappiness | 80 | 35 | 12 | ✅ 已有,值一致 |
| tcp_rmem / tcp_wmem | `4096 32768 2097152` | `4096 262144 16777216` | `4096 524288 16777216` | ✅ 已有 |
| tcp_fastopen | 1 | 3 | 3 | ✅ 已有 |
| tcp_keepalive_time | 18000(5h) | 3600 | 600 | ✅ 已有 |
| qdisc | fq_codel | fq_codel | fq | ⚠️ 字段死(声明未写,见 §4·G2) |
| wifi pm / txqlen | on / 32 | auto / 1024 | off / 3072 | ✅ 已有 |
| **tcp_thin_linear_timeouts + tcp_thin_dupack** | **0** | **1** | **1** | ❌ 缺 → 移(N1) |
| **tcp_rto_max** | **120000** | **15000** | **8000** | ❌ 缺 → 移(N2) |
| **tcp_keepalive_intvl / _probes** | 75 / 9 | 75 / 9 | 75 / 9 | ❌ 只写 time → 移(N3) |
| **tcp_mem** | 动态:`pg=MemTotal_kB/4`,`low=pg*3/100 pressure=pg*6/100 high=pg/10`(全局,非分档) | 同 | 同 | ❌ 缺 → 移(N4) |
| **congestion_control + pacing_ca/ss** | bbr(退 cubic);80 / 110 | bbr;110 / 170 | bbr;160 / 240 | ❌ 缺 → 移(N5) |
| **tcp_early_retrans=3 / tcp_recovery=1 / tcp_slow_start_after_idle=0** | 同(全局) | 同 | 同 | ❌ 缺 → 移(N6) |
| **tcp_notsent_lowat** | 16384 | 32768 | 24576 | ❌ 缺 → 移(N7) |
| **netdev backlog/budget/budget_usecs/dev_weight** | 128/32/1500/8 | 2000/180/5000/64 | 4096/480/1200/160 | ❌ 缺 → 移(N8) |
| rmem_max/wmem_max/optmem_max | 2097152/2097152/16384 | 16777216/16777216/1048576 | 16777216/16777216/1048576 | ❌ 缺 → 移(N8) |
| udp_mem / tcp_mtu_probing / tcp_fin_timeout | `16384 32768 65536`/0/30 | 同/1/20 | `32768 65536 131072`/1/10 | ❌ 缺 → 移(N8) |
| PLB(enabled/idle_rounds/thresh) | 1/2/3 | 同 | 同 | ❌ 缺 → **不移**(§5,节点名杜撰) |
| 安全硬化(rp_filter/redirects/syncookies/rfc1337/icmp/source_route) | 静态全局 | 同 | 同 | ❌ 缺 → **不移**(§5,越权) |

> Kin 现状取证:`SysTweaks.hpp:120-138` 的 `Tune` 只有 11 字段,值与 V57 逐字一致(旧版移植),基础层无漂移。缺的就是上表 ❌ 行。全部节点在上游用 `[ -e … ] &&` 门控,Kin 侧 `wsys→FileWrite` 对不存在节点天然静默失败 = 事实 only-if-exists。

### 维度 2 —— 网络智能(smart 跟日模式 + net_conservative)

两条独立机制,**均无实质可移植价值**:

- **shell 侧「跟日模式」**:smart 档读 `smart_alpha_battery`,`≥800` 视为有效省电,把 `network_stats_poll_interval` 从 30min 放宽到 2h(`asb_reconcile.sh:155-202` 边沿触发)。这是框架 `settings put global` 旋钮,非 sysctl,价值低。**但**它揭示了 Kin 的一个真实行为缺口:Kin 的 auto 档目前不会让 SysTune 跟随自学习结果切档(见 §4·G1)。
- **C 侧 `net_conservative_x1000`**:第 4 个 per-bucket 学习字段(种子表 `asb_smart.h:511-529`、弱信号 +200 `:1622`、会话 `dt_net` `:1859`)。**关键**:该字段在 `asb_governor.c` 被消费 **0 次**(grep=0),也不导出 —— 上游学出来却从不使用,**死字段**。Kin 移植 = 空操作。

### 维度 3 —— VM 调优 + swappiness→sysprop 镜像

V57 `apply_vm()`(`service.sh:479-538`)在写 `vm.swappiness` 后,对三个 OxygenOS 私有属性(`sys.sysctl.swappiness`/`sys.mem.swappiness_on_launcher`/`_on_start`)做**仅当已存在**的 resetprop 镜像(`:482-484`),防 ROM 的 launcher/启动期 swappiness 覆盖本档设置 —— 直击 OnePlus 目标机。其余每档 VM 旋钮(dirty_bytes 绝对值、compaction_proactiveness、lru_gen=7、page-cluster、stat_interval、laptop_mode、watermark_scale_factor、min_free_kbytes)Kin 均未移(现只 swappiness/dirty_expire/dirty_writeback/vfs)。构成一个可选的「VM 增强」次批(§4 Tier 2)。

### 维度 4 —— anti-clamp 厂商钳位「写战」59%→24%

V57 靠三层减少冗余回写:`g_wcache` 写缓存(`force||target!=last` 才写,`asb_writer.h`,**非 V57 新增**,是长期结构)+ 新增「sleep detente」(息屏 + DEEP/LIGHT_IDLE + 外部 owner 持钳 ≥120s + 凉温 → 让步,`asb_smart.h:1379`/`asb_governor.c:5224`)+ smart 迟滞。

**Kin 现状**:按设计原则#2 **天生就不循环重写 `scaling_max_freq`**,让 walt/schedutil 用 up/down_rate_limit 自调,写战率本就低;anti-clamp 检测/软钳/攻击侧已完整移植(headroom 读取、死信号检测、per-cluster 捕获还原、budget/backoff/futility)。V57 的 59→24% 本质是 ASB 在**追平 Kin 早有的写战最小化设计**。三条候选(写缓存去重 / sleep detente / vendor 保持)对抗核验后**全部 reject**:对 Kin 是 no-op 或与既有计数冗余。**无可移植项。**

### 维度 5 —— Smart+FSM 忠实度/漂移(详见 §6)

已移植的 Smart 学习核心与 FSM 时序常量与 V57 逐条一致;但 **FSM 频率天花板有一处真实漂移**(V57 下调),见 §6。

### 维度 6 —— 范围外 + device_bounds + boot 安全

- **`device_bounds_override`**:编译期合成 bounds、默认关、仅 OP15。Kin 已用**运行时读 `cpuinfo_max_freq` 按机型缩放**(`AutoFsm.hpp:241 scale()`,每 tick 乘 `real_hwmax/REF` 并钳到 `[cpuinfo_min, hwmax]`)—— 等价且更通用。**REJECT / 无动作。**
- **bootloop fuse / 49 vendor 文件 / overlay 熔断 / 相机·音频 vendor 改写**:改写 vendor 分区(原则#3),Kin 无此机制、无此类 bug。**范围外。**

---

## 4. 可移植清单(对抗核验后 · 按性价比排序)

改动全部落在 `include/SysTweaks.hpp`(扩 `Tune` 结构 + 几行 `wsys` + 一个 sysprop 镜像 helper),需附 MIT→GPL 署名。`reverts_on_reboot` 全为「是」(纯 sysctl / sysprop,重启还原)。

### Tier 1 —— 网络 for gamers 核心(全 confirm,S 工作量,零设计冲突)

| ID | 做什么 | 精确值(battery / balanced / performance) | 游戏/省电 |
|---|---|---|---|
| **N1** | `tcp_thin_linear_timeouts` + `tcp_thin_dupack` | 0 / 1 / 1 | 游戏低延迟 vs 省电躺平 |
| **N2** | `tcp_rto_max` | 120000 / 15000 / 8000 | 两者兼顾 |
| **N3** | `tcp_keepalive_intvl` + `_probes` | 75·9(三档同) | 省电(配合 keepidle) |
| **N4** | `tcp_mem` 动态按 RAM(C 里读 `/proc/meminfo`,`pg=MemTotal/4`;low 3% / pressure 6% / high 10%) | 动态,全局 | 省电/稳定 |
| **N5** | `tcp_congestion_control=bbr`(退 cubic)+ `tcp_pacing_ca_ratio`/`_ss_ratio` | 80·110 / 110·170 / 160·240 | 吞吐/跟手 |
| **N6** | `tcp_early_retrans=3` + `tcp_recovery=1` + `tcp_slow_start_after_idle=0` | 全局同值 | 低延迟/追帧 |
| **N7** | `tcp_notsent_lowat` | 16384 / 32768 / 24576 | 低延迟 |
| **N8** | 缓冲/收包批:`rmem_max`·`wmem_max`·`optmem_max`、`netdev_max_backlog`·`netdev_budget`·`_budget_usecs`·`dev_weight`、`udp_mem`、`tcp_mtu_probing`、`tcp_fin_timeout`、`somaxconn=512` | 见 §3 表 | 性能/均衡 |

> 建议 **N1+N2+N3 合成一次「SysTune 网络低延迟」提交**先落地 headline;N4~N8 作第二批。

### Tier 2 —— VM 增强(confirm,S,可选次批)

| ID | 做什么 | 精确值(battery / balanced / performance) | 备注 |
|---|---|---|---|
| **V1** | swappiness 镜像三个 OxygenOS sysprop(getprop 非空才 resetprop) | 值=本档 swappiness 80/35/12 | **命中 OnePlus,优先** |
| **V2** | `vm.dirty_bytes` / `dirty_background_bytes`(绝对值,存在则优先于 ratio) | 134217728·33554432 / 67108864·16777216 / 33554432·8388608 | |
| **V3** | `vm.compaction_proactiveness` | 20 / 10 / 0 | |
| **V4** | `/sys/kernel/mm/lru_gen/enabled=7`(全局) | 7 | |
| **V5** | `vm.stat_interval` | 120 / 30 / 8 | |
| **V6** | `vm.laptop_mode` | 1 / 0 / 0 | |
| **V7** | `vm.watermark_scale_factor` / `vm.min_free_kbytes`(ASB shell 有,非 V57 新;核验期误按 C 源判缺,实际 `service.sh:526-527` 每档写) | wmark 150/60/20;minfree 65536/32768/32768 | 低优先 |
| **V8** | `vm.page-cluster`(revise:`apply_vm` 权威值,profile 变量与之不一致) | 3 / 1 / 0 | 低优先,先理顺双写路径 |

### Tier 3 —— 行为缺口(revise)

| ID | 做什么 | 说明 |
|---|---|---|
| **G1** | **auto 档让 SysTune 跟随自学习结果切档** | 现 `applyProfileTune(conf.mode)` 只在 mode.txt 变化时按**用户档字符串**调用;`tuneFor("auto")→kBalanced`,故 auto 引擎内部翻 powersave/performance 时 SysTune **冻在 balanced**。改法:auto 引擎切换离散子档时,以其解析出的 `powersave/balance/performance` **边沿触发**再调 `applyProfileTune`。对齐上游 `asb_reconcile.sh` 的边沿语义,避免每 tick 重写。 |
| **G2** | **修 `Tune.qdisc` 死字段** | `SysTweaks.hpp:23` 声明 `qdisc` 但 `applyProfileTune` 从不写。要么补 `net.core.default_qdisc`(battery/balanced=fq_codel、performance=fq),要么删字段。 |

---

## 5. 明确不移植 / 已具备(对抗核验 reject)

| 项 | 结论 | 一句话理由 |
|---|---|---|
| 基础 per-profile VM/net/WiFi 三元组 | **已具备** | 旧版已移,V57 值与 `SysTweaks.hpp:120-138` 逐字一致 |
| anti-clamp 写战 59→24%(写缓存/sleep detente/vendor 保持) | **已具备且更彻底** | Kin 按原则#2 本就不循环重写 scaling_max,三条候选对 Kin 为 no-op/冗余 |
| `device_bounds_override` | **REJECT** | Kin 运行时 `cpuinfo_max_freq` 缩放(`AutoFsm.hpp:241`)等价/更通用 |
| **PLB**(`tcp_plb_enabled`/`idle_retransmit_rounds`/`retransmit_threshold`) | **REJECT** | ASB 的两个节点名是**杜撰**(mainline 6.1+ 真名为 `tcp_plb_idle_rehash_rounds` 等);8e5 内核多半无此节点,纯 no-op |
| 安全硬化 sysctl(rp_filter/redirects/syncookies/rfc1337/icmp/source_route) | **REJECT** | 影响全局所有流量,超出 CPU 调度器职责 |
| `net_conservative_x1000` | **REJECT** | 上游 `asb_governor.c` 消费 0 次(死字段) |
| smart 时段种子(SLEEP950/LATE750/…) | **REJECT** | 与 Kin「冷启动 alphaRaw=500 中性」设计冲突,照搬是 cargo-cult |
| `smart_dynamic_tune.sh`(按 hint 切 readahead/MGLRU) | **REJECT** | hint=gaming/heavy/… 即场景识别,违反原则#1 |
| `network_stats_poll_interval` 智能切换 | **暂不** | 框架 `settings put global` 旋钮,价值低 |
| Ace6 bootloop / 49 vendor 文件 / overlay 熔断 / 相机 / 音频 | **范围外** | 改写 vendor 分区,Kin 无此机制 |

> 核验副产物:extract 阶段两条候选(`watermark_scale_factor`、`min_free_kbytes` 当作 V57 新特性)被对抗核验判为「非 V57 novelty」而降级 —— 复核发现二者确在 ASB **shell** 里按档写(见 V7),只是非 V57 新增、且不在 C 引擎。已并入 Tier 2 低优先。

---

## 6. Smart/FSM 漂移审计

**结论:已移植的 Smart 学习与 FSM 时序常量与 V57 逐条一致;但 FSM 频率天花板有一处真实漂移(V57 下调),需你拍板。**

### 一致(逐条核对 `asb_smart_defs.h`/`asb_config.h`,无需动)
学习率 `80/1000`;置信门 `350/650`;`EFF_OBS_FULL=800`;衰减 `7/37 天`+floor30%;drain `600s`/`1500`;低电 engage/restore/crit `20/40/10`、force alpha `800/900`;夜间 `0-6`;daypart 边界 `0/6/9/12/17/21/24`;时长权 `25/50/100/125`、信任权 `100/40/15/0`;会话 dt 信号 `+80/+40/+30/+20/+100/−60`;FSM min-dwell `heavy8/sustained24/gaming25`;warmup `grace45/bypass60`。

### 待决 —— **FSM 频率天花板漂移(D1,建议同步)**
V57 `asb_fsm_bounds.generated.h` **下调**了 CEIL(上包络);Kin `AutoFsm.hpp:207-209` 仍是旧的更高值(floor 全部仍一致):

| 界 | Kin 现值(`AutoFsm.hpp`) | V57 新值 | 差异 |
|---|---|---|---|
| battery CEIL 小核 (`BAT_C_MAXL`) | 1804800 | **1670400** | ↓ |
| battery CEIL 大核 (`BAT_C_MAXB`) | 2208000 | **1632000** | ↓ 明显(省电档大核天花板降 ~576MHz) |
| balanced CEIL 小核 (`BAL_C_MAXL`) | 3302400 | **2611200** | ↓ |
| balanced CEIL 大核 (`BAL_C_MAXB`) | 3974400 | 3974400 | = |

因 Kin 会把这套参考包络按 `real_hwmax/REF` 运行时缩放,**下调参考 CEIL 会实际压低本机 battery/balanced 的 FSM 上限** —— 方向与 Kin「省电优先/能降频」一致。这是 Kin 当初照搬的同一张 ASB 生成表被 V57 重新生成/下调,属真实上游漂移。**建议同步**(或至少确认 Kin 是否有意保留旧值)。

### 待决 —— 边角(低优先)
- **D2 文档陈旧**:`AutoFsm.hpp:195` 注释「默认 57/49 同 ASB」已过时 —— 现行默认是 `54/48`(`Config.hpp:152-153` 及各 `Json/*.json`)。改注释即可。
- **D3 热否决温度**:上游 `VETO_CPU_TEMP=60`,Kin `veto_temp=65`(`Config.hpp:81`,注释误称「ASB=65」)。差 5℃、方向安全、已 config 暴露;对齐到 60 或修注释,二选一。
- **D4 第三条预热旁路**:上游新增 `balanced_warmup_bypass_headroom=40`(headroom<40% 也旁路预热),Kin 只移 grace+bypass_temp。但 8e5 上 headroom 常「死」,几乎不触发,低优先。

---

## 7. 建议下一步

1. **先做 Tier 1 的 N1+N2+N3**(SysTune 网络低延迟三件)—— 一次提交落地「network for gamers」精华,`SysTweaks.hpp` 内、only-if-exists、S 工作量、零设计冲突、直击目标机。
2. **再决定 D1(FSM 天花板同步)** —— 这是唯一需要你判断的实质项:是否把 battery/balanced 的 CEIL 同步到 V57 的更低值。
3. 视需要跟第二批 **N4~N8 + Tier 2 VM(V1 优先)**,以及 **G1/G2** 收尾行为缺口与死字段。

> 需要动手时,直接说「**做网络那三项**」(N1/N2/N3)或「**同步 FSM 天花板**」(D1)等即可;实现会附 MIT→GPL 署名、更新 `changelog.md`,并跑一遍 CMake host 构建验证链接。
