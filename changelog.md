# Kin 更新日志

## 前台检测: 参考 fas-rs/Scene/uperf 的通用化优化
- 新增焦点兜底(fas-rs 同款): 无障碍被杀/未开时, 守护经 AOSP 标准的
  mFocusedApp(窗口管理器权威焦点)拿前台, ≥5s 硬限频, 无障碍正常时零开销
- App 推送提速: 事件上报节流 100→50ms, 评估节流 400→300ms, 尾沿补评 450→300ms
  (端到端切换延迟降约 1/3); 输入法动态忽略(Scene 同款)已有, 维持
- 事件驱动架构(uperf 同款 cpuset inotify 即时唤醒)维持不变

## 智能模式: 前台回归"无障碍推送为王" + 游戏保活粘滞(对齐基础模式)
- 前台判定回归简单可靠三件套: App 无障碍推送(第一权威, inotify 秒级) +
  top-app cpuset 校验/直读(标准 Android 层级) + dumpsys(仅手动档) ——
  移除 oom_score_adj 快照仲裁等依赖机型行为的路径, 任何手机通用
- 游戏粘滞改用基础模式同款保活语义: 【游戏进程不死就一直是游戏状态】,
  小窗/分屏/回桌面/焦点横跳都不掉档; 进程退出(手动杀/系统杀/自退)才解除
- 保留: 风驰离线簇补切 / 关核 6s 迟滞 / FAS 换游戏重开会话 / 心跳去重 /
  游戏助手等系统悬浮工具黑名单

## 智能模式: 前台判定重构 FgEngine v2(游戏稳如钉 + 检测快而准)
- 内核真相快照: 前台判定改读 top-app/foreground 两组 cgroup + 每进程 oom_score_adj
  (0=焦点/100=可见/600=回桌面/900=将杀), 废除"取列表最后一行"的碰运气逻辑
- 游戏粘滞状态机: adj 分层判定 + top-app 佐证, 两票墙钟解除(短暂失焦不误杀),
  息屏冻结零开销; 小窗/分屏怎么切, 游戏不退出就一直是游戏状态, 风驰/关核/FAS 全程不抖
- 前台推送与内核快照交叉校验: 卡死的推送 ≥2 票缺席才降级且不会每 10s 复活;
  带前台服务的已退应用不再被误认为还在前台
- 风驰补切: 关核停泊中直接进游戏, 离线簇上线后自动补切风驰调速器(原来会漏切)
- 关核迟滞: 停核需类别稳定 6 秒(升快降慢), 类别毛刺不再引起核心开关抖动
- FAS: 游戏→游戏移交时重开会话(不沿用旧游戏帧率/目标/图层), 预热仍走快节奏
- 桌面等全黑名单场景不再触发 dumpsys 轮询(保持 v1 零 popen 成本模型)

## 智能模式: 游戏粘滞(小窗不再抖) + 前台检测提速
- 游戏粘滞: 进入游戏状态后, 只要游戏进程仍在 top-app 可见组(开小窗/分屏/来回切都算),
  就一直按游戏状态处理 —— 风驰/walt 不再来回切, 关核/档位/FAS 判定同步不抖。
  游戏真正退到后台(回桌面/切走/被杀)或息屏才解除, 照常退出 GAMING。
  在位校验走 cgroup.procs 直读(无 popen), ~1.5s 节流, 不增加每拍开销。
- 前台检测提速: 推送防呆校验节流 10s → 3s; top-app cpuset 一有内核级变化(inotify)
  立即强制校验推送值 —— 推送与真实前台脱节最多存活一个 tick。

## 日志: 对齐 MW_CpuTurboScheduler + FSM 状态行 fsm=xxx
- 默认日志级别 OFF(静默) → INFO(对齐 MW: 开箱即有日志); loglevel 兼容 MW 的 "WARNING" 写法
- FSM 状态切换改在 INFO 级输出「fsm=状态名」(原藏在 Debug, 默认看不见);
  前台心跳行「智能档=」字样同步改为「fsm=」, 状态切换不再重复触发心跳行

## 智能模式: FAS 开关 + 与风驰按应用互斥 + FAS 响应性调优
- App 智能策略「游戏调度」新增 FAS 开关(Function.AutoMode.fas_enable, 默认开)。
- 互斥(按应用): 风驰调速器【真在位】(hm/hmbird/scx 已切成或本就在跑)的 game_heavy
  游戏不走 FAS; 仅 walt/sugov_ext 回退的机型 game_heavy 仍享受 FAS;
  非风驰游戏(game_lite/GPU 进入)照常 FAS; 风驰功能关时 FAS 对所有游戏生效。
- 修复(critical, 审查发现): writeFsmCaps 每拍重写基础 governor, 把刚切的风驰盖回去
  (风驰只生效一拍) —— 现在风驰在位时 FSM 不触碰 governor, 还原后自动恢复重申。
- FAS 响应性: 轮询 250→200ms(掉帧 160→120ms), EMA 1/4→1/3, 单步 400→500MHz;
  预热期也按 FAS 节奏 tick(真实接管 ~12s → ~1.2s, 8s 没接管自动放弃快节奏);
  初始目标锁定加 2 拍确认(防加载页高帧把 60fps 游戏锁成 120); FAS 日志 ~1Hz 节流。
- 顺带: 游戏中关 fsm_enable 时 FAS 会话正确退出(不再带旧状态续跑);
  game_heavy 画像判定收进 applyMtx(修热重载竞态)。

## 前台检测: 推送值防呆(修"卡死的推送有绝对优先级")
App 无障碍推送在极端情况下可能卡死在旧应用且被心跳持续保"新鲜"(App 端 v1.1 已修根因),
此时守护会无限期信任旧包名、永不回退自检 —— 智能模式的分类关核/游戏判定全按旧应用算。
现在每 ~10s 校验一次推送的前台是否仍在 top-app cpuset 组(应用切走后进程会被系统挪去
background 组): 不在 → 作废推送新鲜度并 Warn, 立即回退 cpuset/dumpsys 自检。
小窗/分屏不误伤(主应用与小窗都在 top-app 组); 节点读不到时不判定。
智能模式本就不依赖无障碍: 无推送时自动走 cpuset 直读(auto)/dumpsys(手动档)。

## 线程分配: 规则文件改名 applist.prop + 实时对齐 Metric + 权重配置(cpu.shares)
- 规则文件路径由 `thread_alloc_rules.conf` 改为 `/sdcard/Android/Kin/applist.prop`
  (沿用 AppOpt 文件名); App 侧写新文件时顺手删旧文件。
- 实时调度对齐 Metric: 施加实时前把 `sched_rr_timeslice_ms` 设为 3(RR 时间片更细,
  减少独占卡顿); RT 预算安全网(收窄到周期 95%)保留, 不采用 Metric 的 rt_runtime=-1
  (会让失控实时线程饿死系统)。
- 新增权重配置(`Function.ThreadAlloc.weight_enable` / `weight_shares`, 默认关):
  把绑到大核的关键线程移入 `/dev/cpuctl/top-app/cts-weight` 子组并设 `cpu.shares`
  (默认 4096)抬高算力权重, 高负载下更抢得到 CPU; 离场/关权重/守护退出时把线程移回
  top-app 组。无 cpuctl 的机型自动跳过。

## 智能模式: 游戏状态判定按 perapp 包名策略, 屏蔽 "*" 全局行误判
FSM 进游戏状态(gameHint 秒进)的判据 = perapp_powermode.txt(App「应用策略」写入,
匹配时 perapp 优先于 config 游戏库): 包名策略为 game_heavy/game_lite(is_game 画像)
即进游戏状态。改用显式匹配(useGlobalFallback=false) —— 修复 "*" 全局默认行被设成
游戏档时【所有 App】都被判成游戏进 GAMING 的误判。GPU≥90/系统类别=GAME 兜底不变
(未分配策略的游戏仍能进游戏状态, 但风驰调速器仍只认 game_heavy)。

## 风驰调速器: 收敛到 game_heavy 档 + 扩充支持游戏列表
gaming_gov 切换现在只对【前台命中 game_heavy(风驰游戏)画像】的应用生效:
智能模式要求 FSM=GAMING 且前台∈game_heavy; 画像模式命中 game_heavy 档即切、
换前台/游戏退出/黑名单释放时条件还原。出厂配置(全机型 Json/ + magisk/config/)的
game_heavy 包列表并入风驰内核支持清单(原神/星铁/绝区零/鸣潮/王者/和平精英/
三角洲/LOL手游/CODM/FIFA/Roblox/雀魂/蛋仔派对 等 36 款, 去 .enc 后缀)。

## 智能模式: 游戏态切换内核调速器(gaming_gov, 风驰内核 hm/scx)
FSM 进入 GAMING 时把各簇 `scaling_governor` 切到 `Function.AutoMode.gaming_gov`
(逗号偏好列表, 默认 `hm,hmbird,scx` —— hm 全名 hmbird)中本机
`scaling_available_governors` 里存在的第一个; 列表全不可用时回退 walt(高通)/
sugov_ext(天玑等)。离开 GAMING / 退出智能模式 / 配置热重载重预置(primeAutoFsm) /
守护退出时【条件还原】—— 只有当前值仍是我们写入的才还原, 外部(切档/画像/第三方)
改过则不覆盖。写入同步 FreqWriter 去重缓存, 防止后续同值写被错误短路。
键写空 = 功能关; App 智能策略页新增「游戏状态调度」开关。

## 线程分配: 新增应用黑名单(blacklist_pkgs)
配合 App 线程界面重做(Metric 式布局)新增 `Function.ThreadAlloc.blacklist_pkgs`
(逗号分隔包名): 命中的应用在三条路径统一跳过 —— 规则引擎(配了规则也不绑核)、
游戏库快路径、实时调度(SCHED_FIFO/RR)。进程名含 `:sub` 后缀时取主包名匹配。
顺带修正: `resetState()` 现在整段复位 ThreadAlloc 字段, 热重载删键后旧的
game_pkgs/blacklist_pkgs 列表不再残留(config 解析失败时回落到线程分配关闭的安全态)。

## 线程分配: 执行权归一守护, 移除 eBPF/事件监听
绑核执行统一由守护负责(前台变化由 App 无障碍推送 topapp.txt 精准驱动, 新线程由
interval_s 轮询覆盖); App 侧线程规则服务降级为守护缺席时的兜底。移除从未默认启用的
eBPF 可选后端(src/bpf/ + CMake CTS_EBPF)与 proc connector 事件监听线程
(EventMonitor.hpp、ThreadAlloc.ebpf_enable 配置键), 遗留配置键由 App 写回时自动清除。
夜间窗口一旦学成会覆盖 config 的 `night_start/night_end`, 学偏了用户改配置也没用——现在
App(问卷"时段不准")可经 `auto_control.txt` 直达学习数据纠正:
- **night_seed=<入睡分>,<起床分>**: 两个分钟数(0..1439, 如 `1380,420` = 23:00→07:00)直接写入
  学习窗口并补足样本门槛立即生效, 后续 EWMA 从纠正后的锚点继续细化; 参数非法仅 Warn 忽略。
- **night_reset**: 清空入睡/起床/样本数, 交回自动学习(重新满 3 晚才生效)。
- 两条命令均清掉旧窗口的待确认候选/黑屏累计(避免污染), 并立即持久化到 `auto_night.txt`。
仅动 `include/AutoMode.hpp` 的 `processControlFile()`, 既有 reset/reset_bucket/export 语义不变。

## 智能模式: 每状态频率/GPU 上限可自定义(fsm_policy)
让用户按 FSM 状态自定义智能模式的频率天花板, 不再只能吃内置曲线。config.json 新增
`Function.AutoMode.fsm_policy` 节点, 按状态名(`deep_idle/light_idle/moderate/heavy/sustained/gaming`)
逐状态可选三键(值与 app_policy 同约定写字符串, 如 `"75"`):
- **maxL / maxB**: 该状态小核/大核 scaling_max 上限 = 本机 cpuinfo_max_freq × 百分比(合法域 10..100;
  缺失/越界 = 不覆盖, 保持内置 FSM 包络; 下限只压不抬, 不会高于新上限)。
- **gpu**: 该状态 GPU 上限百分比(合法域 0..100; 0/缺失/越界 = 用内置每状态值)。
- **生效顺序**: 用户覆盖 → FAS(游戏帧率闭环) → App 类别钳制(只压不抬), 后两层语义不变。
- 无总开关: 节点缺失/为空 = 完全无覆盖, 行为与旧版一致; 热重载删键即恢复内置, 不残留旧值。
- 仅动 `include/Config.hpp`(覆盖表)/`include/JsonConfig.hpp`(解析)/`include/scheduler.hpp`(应用),
  出厂 config 不带此节点(装机不覆盖用户已有配置)。

## 移除第三方调度平台检测 + 日志显示 App 名字
- **移除 Scene(第三方调度)接管检测**: 之前"检测到 Scene 就退回基础档、不叠加 App 画像"的判定会误伤——
  只要 Scene 装着/进程活着/开了无障碍就让权, 导致【App 画像用不了】。现彻底删除 `refreshSceneActive` 与
  `sceneActive_` 的全部门闸(`include/scheduler.hpp`): `applyWithProfile` / `evalForegroundOnce` /
  「跟随全局 perapp *」都不再判 Scene, App 画像与全局档无条件生效。(App 端一并移除了线程分配页的
  "检测到可能的冲突"横幅与 `ThreadAllocator.conflicts()`。)
- **日志显示 App 名字**: INFO 日志"前台: X → 模式"里的 X 现显示 App 名字而非包名。App 在 MainActivity 打开时
  把 pkg→App名字 映射 base64 写到 `/sdcard/Android/Kin/AI/pkg_label.txt`; 守护 `loadAppLabels()` 加载
  (inotify 热重载, 与 perapp/topapp 同一套目录监听), 打日志时经 `labelForLog(pkg)` 翻成名字, 读不到退回包名。
验证: NDK r28c(C++23)全量交叉编译 arm64-v8a 通过, 二进制 736 KB。

## 模式切换 INFO 日志: 前台应用 → 落到哪个档
让用户开 `loglevel=INFO` 后能从 `/sdcard/Android/Kin/log.txt` 看清"哪个应用被切到了哪个模式"。
边沿触发(仅在前台/档变化时打一行), 不刷屏; 默认 `loglevel=OFF` 不产生任何日志。
- **基础模式**: 前台每换一个应用都打一行「前台: 包名 → 画像档[名] / 全局档[名] / 基础档[powersave|balance|
  performance|fast]」——之前基础档那行不显示具体是哪个档, 且被去重挡住(切到无画像应用时不打), 现改为前台一变就打
  (写 sysfs 仍按去重, 档没变不重复下发)。
- **智能模式**: FSM 档(`DEEP_IDLE/LIGHT_IDLE/MODERATE/HEAVY/SUSTAINED/GAMING`)变化时也打 INFO
  「前台: 包名 类型=N 智能档=状态」(之前 FSM 状态只在 Debug 打); 整档子模式的「自动模式: 旧→新」行补上前台包名。
- 仅动 `include/scheduler.hpp` 的日志文案与触发条件, 不改任何调度写路径。

## Scene(vtools)接管时只用基础档
当机上装有并运行「Scene / 幕帘」(`com.omarea.vtools`, 一套同类的调度接管工具)时, 两套接管会互相
覆写、打架。现在 Kin 检测到 Scene 在跑就【只跟随基础档(mode.txt 的 powersave/balance/performance/fast)】,
不再叠加 per-app「应用画像(AppProfile)」, 把每应用精细调优的主导权让给 Scene, 避免来回抢。
- **判据「真正接管」而非「装了/在跑」(避免误罢工)**: `refreshSceneActive()` 现读
  `settings get secure enabled_accessibility_services`, 列表含 Scene 主体 `com.omarea.vtools` 才判为接管。
  依据: Scene 的 per-app 自动切档依赖它【自己的无障碍服务】拿前台事件流; 用户没在系统「无障碍」里开启 Scene 时,
  Scene 进程可以活着却无法自动接管——此时 Kin 不该让权, 应照常按画像调度。只认做 CPU 调度的主体 vtools;
  gesture(纯手势)/gamepad(游戏悬浮窗)即便开了无障碍也不据此让权。4s TTL 缓存; 没装 vtools 时免跑 settings。
  取舍: 个别走 root/dumpsys 取前台、不开无障碍的 Scene 变体会漏判——按"宁可少让权、不误罢工"处理。
- **接管语义**: Scene 在跑时 `evalForegroundOnce` 把 `currentMatch` 恒置 -1(与 auto 模式独占同款),
  `applyWithProfile` 只落 `applyBaseMode()`; 无匹配「跟随全局 perapp *」也一并跳过。Scene 退场后自动恢复画像。
- 仅新增 `include/scheduler.hpp` 的门控判断, 不改任何调度写路径; 无 Scene 环境行为零变化。
验证: host g++ `-Wall -Wextra -Wshadow` 隔离 mock 编译通过, Scene 未装时 `sceneActive_` 恒 false。

## 事件驱动 (eBPF / 内核事件监听)
参考 AppOpt 的 eBPF 实时版, 新增「事件驱动」模式: 进程/线程一被创建(fork/exec)就【即时】触发一次绑核/
实时优先级应用, 不必干等下一个轮询周期(平均延迟从秒级降到几百毫秒内)。独立开关, 默认关。
- **默认后端: 内核 proc connector(新增 `include/EventMonitor.hpp`)** —— 用 `NETLINK_CONNECTOR`+`CN_IDX_PROC`
  多播收 fork/exec 事件, **无需 eBPF 工具链/BTF**, 用现有 NDK 就能编, 覆盖的安卓内核(`CONFIG_PROC_EVENTS`)远比 BTF 广。
  打不开(内核未开/无权限)则自动退回纯轮询, 零副作用。
- **可选后端: 真·eBPF(新增 `src/bpf/monitor.bpf.c` + CMake `-DCTS_EBPF=ON`)** —— 挂 tracepoint,
  额外能监控 `sched_setaffinity` 抢回亲和; 需 clang-bpf/bpftool/libbpf + 内核 BTF, 详见 `src/bpf/README.md`。
- 实现: `eventMonitorTask` 收到事件 → 攒批 200ms 合并同批 fork → 叫醒 `threadAllocTask` 立即重应用;
  周期全量扫描仍作兜底。独立开关 `Function.ThreadAlloc.ebpf_enable`(默认关), 在 App「线程分配 → 实时监听」开启。

## 实时调度(SCHED_FIFO/RR)
参考 SutoLiu 的 AppOpt Ebpf-Prio 版(`priority.prop`)新增线程实时调度: 给关键线程(渲染/游戏主线程)
更高的调度类, 让它抢占普通线程, 更不掉帧。与绑核正交、独立开关。
- **规则引擎(新增 `ThreadPriorityRules.hpp`)**: 解析 `包名{线程}=SCHED_FIFO 优先级`(兼容 AppOpt priority.prop)。
  策略 SCHED_OTHER(0) / SCHED_FIFO(1-99) / SCHED_RR(1-99); 匹配语义同绑核引擎(精确>[..]>?>*>默认)。
- **应用**: `threadAllocTask` 扫 /proc 对命中进程逐线程 `sched_setscheduler`, FIFO/RR 带 `SCHED_RESET_ON_FORK`
  (子线程不继承实时); 幂等; 规则文件 `/sdcard/Android/Kin/thread_priority_rules.conf` 按 (mtime,size) 热重载。
- **安全网**: 开启时校验内核 RT 预算 `sched_rt_runtime_us`, 若为无限(-1)/0/超周期则收窄到周期 95%,
  给普通线程留 5% 兜底, 防实时线程把系统饿死。离场/关闭/退出逐 tid 还原为 SCHED_OTHER(不误伤系统 RT 线程)。
- 独立开关 `Function.ThreadAlloc.rt_enable`(默认关), 在 App「线程分配 → 实时调度」开启, 规则在「实时优先级规则」页配置。

## 260708b
承 260708a 的 ASB V57 研究(docs/asb-v57-research.md), 收尾行为缺口与忠实度小项:
- **G1 · auto 档让 SysTune 跟随自学习基础档**: 此前 auto 模式下 SysTune(每档 VM/网络/WiFi)恒冻在均衡档——`applyProfileTune(conf.mode)` 只按用户档字符串下发, `tuneFor("auto")→均衡。现在 auto 引擎每 tick 解析出的基础档(powersave/balance/performance)变化时, 边沿触发重下发对应档 SysTune(移植 ASB V57 smart 跟档语义: alpha 高→省电网络集、低→低延迟)。FSM 与非 FSM(整档)两条路径都覆盖; 复用本 tick 已算 alpha 的 `decide(m, alpha)` 重载, 不二次喂 alphaFor; 边沿去重(`lastAutoTuneMode_`, 持 applyMtx), 纯 sysctl 不进逐 tick 写循环; 退出 auto 清去重、重进按当时基础档重下发。
- **D3 · 热否决温度对齐上游**: `veto_temp` 65→60(上游 asb_smart_defs.h ASB_SMART_VETO_CPU_TEMP_C=60; 原注释误称"ASB=65"系笔误)。方向= 更早热否决, 更热安全; 仍 config 可调。
- **D2 · 修陈旧注释**: AutoFsm.hpp SUSTAINED 进/出温度注释"默认 57/49 同 ASB"已过时——现行默认为 54/48(ASB V54 热经济档, Config.hpp:152-153), 更正注释, 不改行为。
验证: host clang++ 全量 TU(src/main.cpp)-fsyntax-only 通过、-Wall -Wextra -Wshadow 下改动文件零告警(仅 vendored Json/ 库有既存 shadow 告警, 非 -Werror 不阻断); 真机 NDK 构建与实测待验。

## 260708a
对照上游 ASB V57「network for gamers」真源码(krilikd/AutoSystemBoost, MIT © Dima Krylov)研究后, 移植 3 项(研究报告见 docs/asb-v57-research.md):
SysTune 网络增强(移植 profiles/*.sh + service.sh apply_net, 全部 only-if-exists 纯 sysctl, 重启自还原, 仅 mode 变化时下发不入 tick 循环——不构成写战):
- **每档 thin/RTO 差异化**: tcp_thin_linear_timeouts+tcp_thin_dupack(省电 0 / 均衡·性能 1)、tcp_rto_max(省电躺平 120000 / 均衡 15000 / 游戏低延迟 8000ms)——省电档让连接躺平、游戏档抢低延迟。
- **keepalive 三元组补齐**: 现只写 keepalive_time, 补 intvl=75 / probes=9。
- **动态 tcp_mem**: 读本机 /proc/meminfo MemTotal, 按 页数=kB/4、low 3% / pressure 6% / high 10% 下发(非分档)。
- **BBR + pacing**: 探测 tcp_available_congestion_control, 有 bbr 用 bbr 否则退 cubic; tcp_pacing_ca/ss_ratio 按档(80·110 / 110·170 / 160·240)。
- **缓冲/收包批 + 延迟旋钮**: rmem_max/wmem_max/optmem_max、netdev backlog/budget/budget_usecs/dev_weight、udp_mem、tcp_mtu_probing、tcp_fin_timeout、tcp_notsent_lowat、somaxconn=512、tcp_early_retrans=3 / tcp_recovery=1 / tcp_slow_start_after_idle=0——值逐档对照 V57 真值。
- **修 qdisc 死字段**: Tune.qdisc 此前声明未写, 现补写 net.core.default_qdisc(省电·均衡 fq_codel / 性能 fq)。
- **不移植**(对照核验后): PLB(ASB 节点名与 mainline 不符、8e5 多半无此节点)、安全硬化 sysctl(rp_filter/redirects/… 影响全局流量、超出调度器职责)。
swappiness 系统属性镜像(移植 V57 service.sh:482-484):
- 写 vm.swappiness 后, 对 sys.sysctl.swappiness / sys.mem.swappiness_on_launcher / sys.mem.swappiness_on_start 做**仅当已存在才 resetprop**的镜像(不新建属性), 防 OxygenOS launcher/启动期 swappiness 覆盖本档设置——直击 OnePlus 目标机。
FSM 频率天花板对齐 V57(移植上游重生成的 asb_fsm_bounds.generated.h):
- 上游 V57 下调 3 个 CEIL 上包络: battery 小核 1804800→1670400、battery 大核 2208000→1632000、balanced 小核 3302400→2611200(floor 与其余 CEIL 全部不变, 参考系一致——12 界中 9 界原本就吻合)。方向= 省电档/均衡档更早降频, 契合本项目"省电优先/能降频"定位; 仍按本机 cpuinfo_max_freq 运行时缩放。
说明: 本批为对照上游研究后的忠实移植(MIT→GPL-3.0 兼容, 署名见各改动处注释)。host clang++ 全量 TU(src/main.cpp)语法校验通过; 真机 NDK 构建与实测待验。

## 线程分配(AppOpt 式规则引擎)
参考 SutoLiu 大佬的 AppOpt(GPL3.0, https://gitee.com/sutoliu/AppOpt)完善线程功能: 守护从"只把前台游戏的
渲染线程绑大核"升级为**读用户规则、逐线程绑核**的完整引擎。
- **规则引擎(新增 `ThreadRules.hpp`)**: 解析 `pkg{线程名}=核范围`(与 App 的 `thread_alloc_rules.conf` /
  AppOpt `applist.conf` 完全兼容)。进程名支持通配 `* ? [..]`; 线程名优先级 **精确 > `[..]` > `?` > `*` >
  进程默认(空线程名)**; 越界核号的规则整条作废。
- **持续生效**: `threadAllocTask` 扫 `/proc`, 对 cmdline 命中规则的**所有进程**(系统进程/普通应用/游戏均可)
  逐线程 `sched_setaffinity`; 只动亲和, 绝不碰频率/governor/GPU/online。
- **热重载**: 规则文件 `/sdcard/Android/Kin/thread_alloc_rules.conf` 的 mtime 变化即重载(App 改规则立即生效, 无需重启)。
- **幂等 + 离场还原**: 与目标亲和一致的线程跳过; 关闭/离开命中/进程退出/守护退出时把绑过的进程还原为全核。
- **兼容旧体验**: 保留"前台游戏(游戏库/画像 isGame/系统类别 GAME)且未配规则 → 渲染线程绑大核"的零配置快路径。
- 默认关闭(`Function.ThreadAlloc.enable=false`), 在 App「线程分配」页开启。

## 260629e
Fable5 ultracode 全面体检(8 视角查找→10 项验证确认)后修复全部 10 个问题:
FAS(帧感知, 默认开——影响所有人):
- **目标帧率上锁**: 重定目标改用【8s 稳态长窗均值】且要求【连续高于目标达 20s(墙钟)】才升 —— 菜单/加载页的
  短时高帧(常持续数秒)不再把目标永久锁高(原会导致回到正常帧率后每拍误判掉帧、频率钉死满档白烧电);
  真持续高帧的游戏仍在 20s 后正确上修(只延后不误伤)。经对抗验证确认按时长计才能挡住 4s+ 加载页(tick 计数不够)。
- **一次卡顿钉死满频**: 重掉帧的"顶满"只作用在【本拍输出】, 不再写回控制器积分器 —— 一次瞬时卡顿抖动过后
  立即回到真实工作点, 不留后患; 真持续掉帧仍由 P 项数拍内爬满。
- **省电慢环误伤 CPU-bound 游戏**: 忙闲判据由整机平均 cpuBusy 改为 max(CPU,GPU); 下压封顶=JANK_FPS_GAP,
  保证设定点恒高于掉帧线(30fps 档也不自造掉帧); 改整数十分位计数消除浮点漂移。
anti-clamp 攻击侧(默认关):
- **自遮蔽误判节点死**: 抢频后本节点回读=自写满频, 新增 acRaised_ —— 厂商夺回(读到<100)前满频读数视为
  不可信(不计死信号、不参与判定), 修复"开攻击侧后首次抢频即关掉整个厂商压频检测/软钳补偿"。
- **切档不还原**: 新增幂等 antiClampRelease(), 在【离开 auto / 关 FSM / 探测窗退出】等 antiClampTick 不再被
  调用的路径上收手还原, 修复"抢频中切手动档→厂商热钳被永久顶在满频"。
- **按簇捕获+还原**: 进入对抗时逐簇捕获厂商钳值, 收手按簇精确还原(而非用小核单一比例套所有簇) —— 修复
  "过热收手反而把大核解钳到超过厂商意图"。
- **逐核回写**: 厂商节点是 per-CPU freq-QoS(簇上限取各核最小), 改逐核写不只写簇首核, 防厂商逐核压频时抢频空操作。
FSM:
- **预热宽限失效**: 惰性初始化会话起点 —— 开屏状态下启动守护/开关时首个会话的预热宽限不再整局失效;
  wall-clock 回拨(age<0)按宽限结束处理(安全方向)。
清理: 删除重复的 Utils::WriteStr, 改用既有 FileWriteBlocking(带 chmod 回退+重试+成功校验)。

## 260629d
对照上游 ASB(krilikd/AutoSystemBoost) 真源码补 2 项(逐文件核对 asb_fsm.h / asb_smart.h / asb_governor.c):
- **FSM 预热宽限(warmup grace, 移植 ASB balanced_warmup_grace_s=45)**: 亮屏会话开头 grace 秒内
  抑制进 SUSTAINED 热降档, 避免"刚开游戏/应用的加载发热尖峰"把人卡一下; 温度≥fsm_warmup_bypass_temp
  (默认60℃)则旁路照常降, 不牺牲热安全。config 新增 fsm_warmup_grace_s / fsm_warmup_bypass_temp(带护栏)。
- **anti-clamp 攻击侧补预算/退避/futility(对照 asb_governor.c 的 budget/BACKOFF/futility)**: 防"写了没用"
  的机器每个游戏会话无限抢频。每会话给 anticlamp_attack_budget(默认6)次抢频预算, 耗尽→退避
  anticlamp_attack_backoff_s(默认30s)再回满重试; 累计退避达 anticlamp_attack_max_backoffs(默认2)次仍耗尽
  → 本会话彻底放弃(只遥测)。离开游戏态/过热收手/关开关时整会话复位。遥测 anticlamp_attack 增值 3=退避/已放弃。
- 核对确认已吻合(不改): FSM 六状态/up2 down5 窗口/min-dwell(8/24/25s); Smart 学习率80/置信/eff_obs/衰减/
  dt_alpha 信号/分级回退/eff_scale 门控。有意差异(保留): SUSTAINED level 0.62(上游 0.84, 我方真压频)、
  GAMING GPU 阈值 90(上游65, 适配高刷屏)、memory_pressure 属 shell 不入 C 守护。

## 260629c
对照上游 fas-rs(shadow3aaa) 真源码补 1 项省电特性 —— util 驱动的"目标帧率下修"慢环:
- 移植 fas-rs `adjust_target_fps`(controll.rs): CPU 有明显余量(占用≤55%)且帧达标时, 每秒以 ±0.1
  缓动把有效目标帧率最多下压 3fps → 频率随之降一点, 回收限帧/轻量游戏的浪费功耗; 占用≥65%缓回满目标,
  近乎空闲(≤10%, 过场/暂停)复位。**掉帧立刻收回到 0**(性能优先, 绝不让游戏变差)。
- 与上游对照确认我方移植已吻合的部分(不改): 纯 P 控制器 kp=3e-4、归一化误差(帧时×目标fps−1s)输出 kHz、
  增量积分到运行设定点、掉帧旁路 util 上限(cur×util/0.5)。差异项(写 scaling_min 钉热簇/per-frame 帧时/
  100ms stall 中途反应)因与本项目"不碰下限、帧源为 ~1s 均值 sysfs"理念/条件不符, 不照搬。

## 260629b
游戏 FAS(帧感知调频)优化 4 项 —— 更跟手、掉帧追得更快、CPU-bound 不误压、稳态更省电:
- **控制信号去滞后**: 误差项/掉帧判定从 8s 长窗均值(250ms×32 拍)改用响应快的 EMA(~1s),
  重场景下追帧从"好几秒"缩到~1s。长窗只保留作目标 tier 的稳态锁定。P 项仍有单步钳制+util上限+地板,
  更灵敏也不会震荡。自学目标上修的重探同步改用 EMA(仍需 8 拍确认, 不误升)。
- **重掉帧快追**: 实测帧率<目标×0.85(掉>15%)判重掉帧, 一拍直接顶到本簇硬件最高频追回
  (安全方向; 随后由静态上限/热钳自然回落), 不再受单步 +400MHz 限制慢慢爬。
- **CPU-bound 不误压**: util 上限的"负载"代理由"只看 GPU"改为 max(CPU 实活, GPU) —— CPU-bound
  游戏(GPU 低但 CPU 忙)不再被误判成"芯片不忙"而压低 CPU 频, 消除掉帧↔解钳的来回震荡。
- **自适应轮询**: 掉帧→160ms 快查; 明显高于目标且稳→450ms 放慢省唤醒/省电; 否则常速 250ms。
- GPU 瓶颈(≥95%)护栏改为"任何抬频都按兵不动"(含重掉帧), 该升的是 GPU 而非 CPU, 省电更准。
  tick() 新增入参 cpuBusy(调度层 mx.cpuBusy 注入)。行为仍受 fas_enable 总开关, 默认行为不变。

## 260629a
anti-clamp 移植第 2 步: 补齐【攻击侧】主动对抗抬频(默认关, 待真机确认 headroom 信号活后再开):
- **攻击侧状态机**(AutoMode.hpp antiClampTick): 仅游戏态(FSM=GAMING)且检测到疑似厂商压频
  (vendor_clamp=1, 即硬钳且温度不高)时, 回写厂商节点 msm_performance/cpu_max_freq 把各簇上限
  顶回本簇硬件最高频(每簇用首核=policy 号代表, 格式 "pol:freq"), 抢回被无谓压掉的频率。
- **热优先收手**: 温度≥anticlamp_attack_temp(默认46℃) / 离开游戏态 / 关总开关 → 立即停抢,
  并按进入时捕获的真实 headroom 比例还原各簇钳值, 把控制权安全交回厂商(绝不把过热机器解钳顶满)。
- **自遮蔽处理**: 抢频后节点回读恒满频(headroom=100), 故进入对抗后按 anticlamp_attack_hold
  (默认8 tick)硬保持、忽略被自写遮蔽的读数; 到点停写 anticlamp_attack_probe(默认4 tick)探真值
  再决定复抢 —— 避免每 tick 抖动。断言期间不计入死信号检测、fillClampMetrics 标 headroom_valid=0。
- **写在 writeFsmCaps 之后**(scheduler.hpp applyAntiClampNode): 厂商节点放开后, 刚写的 scaling_max 才真生效。
  新增 Utils::WriteStr(写 "cpu:freq" 格式串)。
- 遥测: auto_status.txt / FSM 状态新增 anticlamp_attack(0无/1抢频/2收手) 与 anticlamp_attack_total(累计抢频 tick)。
- config 新增 anticlamp_attack_enable(默认 false) / anticlamp_attack_temp / anticlamp_attack_hold /
  anticlamp_attack_probe(均带护栏)。默认关, 行为与第 1 步完全一致, 不影响现网。

## 260628d
工程债(第 3 批): 抽 SoC 适配层(HAL), 据锐评"强绑定单平台 / 给 MTK 其它骁龙留扩展点":
- 新增 include/Soc.hpp: 把所有机型相关 sysfs 路径与参考常量集中一处 ——
  measured_fps(帧源) / GPU 占用 / 电流 / 背光 / headroom 节点 / 温度传感器关键字 / REF_HWMAX_LITTLE,BIG。
- AutoMode.hpp / AutoFsm.hpp / AutoFas.hpp 改为引用 Soc::*, 不再内嵌硬编码路径; 行为完全不变
  (路径逐字搬迁, 仍是运行时逐个探测命中即用)。适配新平台只改 Soc.hpp 一个文件。
- 注: "拆 1170 行巨类"属纯组织性重构、收益低风险高, 待可真机构建验证时再增量进行, 本批不动。

## 260628c
学习科学补强(第 2 批): 置信度方差修正 + eff_obs 滑窗(据锐评"conf 其实只是样本计数 / eff_obs 封顶永驻"):
- **桶内一致性(MAD)**(AutoMode.hpp): 每桶跟踪方向信号 dt 的 EWMA 均值与平均绝对偏差(均×100 定点, a=1/8)。
- **置信度方差修正**: confOf 在样本计数×时间衰减之上, 再乘"一致性因子"(MAD=0→1.0, MAD=mad_scale(默认60)→0.5)。
  满 3 会话后生效; 高方差桶(通勤/开会忽闲忽忙)置信被压低 → 更早回退到稳定邻桶/全局, 不再被虚高样本数带偏。
- **eff_obs 滑窗**: 累计从"硬封顶 4000 后永驻"改为每会话先按 obs_retain_pct(默认97%)衰减再累加(近期权重更高);
  CAP 仅作安全上界。
- 持久化: auto_state.txt 增 dt_mean / dt_mad / dt_samples 三字段(共 7 字段, 向后兼容旧 4/5 字段)。
  reset/reset_bucket 一并清这些; auto_status 增 consistency 字段; auto_export 增 consist / dt_samples 列。
- config 新增 conf_var_enable / mad_scale / obs_retain_pct(带护栏); 全部默认值使行为平滑过渡, conf_var_enable=false 即回旧行为。

## 260628b
anti-clamp 移植第 1 步: 厂商压频检测 + 遥测 + 软钳降激进(据上游 ASB 真源码 asb_metrics.h / asb_governor.c):
- **headroom 读取**(AutoMode.hpp): 读 /sys/kernel/msm_performance/parameters/cpu_max_freq 算 headroom%
  (厂商强制 CPU 上限 ÷ 本机 policy0 硬件最大频, hw max 自动读 cpuinfo_max_freq 不写死)。
- **死信号检测**(原样移植 ASB): SM8850(8 Elite Gen5)上该节点常恒报满频 → 连续 10 次 100% 判死信号、
  标 headroom_valid=0 只遥测不参与决策, 连续 5 次合理读数恢复。信号不可信时全程旁路, 不会乱调频。
- **vendor-clamp 检测**: headroom<soft_pct(默认70)=软钳; <hard_pct(默认45)且温度不高=疑似厂商压频,
  维护滚动 1h 计数。软钳时按 ASB soft_clamp 语义略上抬 alpha("降激进"安全方向, anticlamp_backoff 可调/置0)。
- **遥测**: auto_status.txt / FSM 状态新增 headroom / headroom_valid / soft_clamp / hard_clamp /
  vendor_clamp / vendor_clamp_1h 六字段, 供 App 与诊断观察。
- config 新增 anticlamp_enable / anticlamp_soft_pct / anticlamp_hard_pct / anticlamp_backoff(带护栏)。
- **主动"抬频对抗"攻击侧暂未启用**: 需先用本层遥测在真机确认 headroom 信号在 SM8850 上是活的(ASB 自陈常死),
  再决定是否上对抗逻辑。新增 fd slot 17(headroom), kFdSlots 16→18。

## 260628a
学习科学补强(第 1 批, 据锐评补"纯利用零探索 / 无奖励闭环 / 结构偏保守 / dt 硬编码"四大缺点):
- **会话方向信号 dt 系数可配置**: 把原硬编码的 6 个经验值(极热+80/热+40/高耗电+30/持续重载+20/
  热否决+100/干净凉爽−60)拉成 config 的 dt_vhot/dt_hot/dt_drainy/dt_sustained/dt_thermal_hit/
  dt_clean_cool, 缺省值不变(=ASB 真源码), 是"reward 数据驱动"的第一步。
- **ε-greedy 探索 + 奖励闭环**(AutoMode.hpp): 仅在已门控(conf≥350)的桶里以 explore_pct%(默认 4%)
  给会话 alpha 加 ±explore_offset(默认 60)小扰动, 方向偏性能侧(补回缺失的性能侧学习动力);
  会话结束用实测放电均流对比每桶基线(新增 base_cost EWMA)算弱 reward, 给 alphaRaw ±reward_step
  永久微调。扰动只在覆盖层之前施加, 温控/低电/夜间安全地板仍会钳回 → 不破坏温控安全。
  explore_enable=false 即回到原纯利用行为。
- **学习重置 / 导出诊断入口**: 写 auto_control.txt 一行命令 reset / reset_bucket=N / export,
  守护 ~5s 内消费删除; export 把每桶 alpha/conf/基线写到 auto_export.txt(可证伪/可观测)。
  作息突变(换班/差旅)可快速纠偏, 无需重装。
- auto_state.txt 增第 5 字段 base_cost(向后兼容旧 4 字段); auto_status.txt 增 explore / base_cost
  字段供 App 显示。config 全部新键有解析护栏与默认值, 不配则行为完全同旧版。
- 仍**未移植** anti-clamp / 预算自校正 / 跨设备先验(anti-clamp 需上游 ASB 源码与真机, 待源码导入后单独处理)。

## 260625e
自动模式(智能档)配置开关化 —— 把 ASB 的「自动省电 / 夜间安静 / 温控阈值」做成 App 可调开关,守护按需生效:
- config 新增 Function.AutoMode 四键(均可在 config.json 覆盖, 无需重编译):
  - auto_battery_enable(默认 true): 低电(≤low_batt)是否自动偏省电; false=不因低电强制上抬 alpha。
  - night_quiet_enable(默认 true): 夜间窗口息屏是否强制省电; false=夜里不特殊处理。
  - fsm_sustained_enter / fsm_sustained_exit(默认 57 / 49 ℃): FSM 主动调频的热限进/出温度,
    原为 AutoFsm.hpp 硬编码常量,现读 config 暴露给用户调"多热开始压频 / 多凉才松手"。
    解析时护栏: enter 必须 > exit, 否则回退 57/49。
- 夜间覆盖改用可配置窗口 [night_start, night_end)(支持跨零点)按真实小时判定,替换原先固定的
  daypart 近似(0/5/1 时段),与 config 默认 0~6 一致, App 的"夜间时段"滑块即可控制。
- 低电覆盖与夜间覆盖分别受上面两个开关门控; 低电迟滞闩仍持续更新避免切换抖动。
- 新增 magisk/ 模块骨架(脚本/配置), 并让 service.sh 合法性守护与 vtools/powercfg.sh 接受 "auto" 档,
  使 App 可一键切到智能档(此前 auto 会被脚本判为非法值纠回 balance)。守护二进制/APK 为构建产物不入库。

## 260625d
移植 ASB 非 CPU 系统优化(新增 include/SysTweaks.hpp, Function 三开关控制):
- SysTune(默认开): 每档 VM/网络/WiFi 调优 —— swappiness/dirty_*/vfs、tcp_rmem/wmem/fastopen/
  keepalive、wifi power_save/tx_queue_len; 值取自 ASB profiles/{battery,balanced,performance}.sh,
  Kin 档位映射(powersave→battery, balance→balanced, performance/fast→performance, auto→balanced)。
  纯 sysctl, 重启自还原; 在 applyAllConfig/Init 按 conf.mode 应用。
- BootTrim(默认关): 开机一次性 pm disable 4 个纯分析服务(midas/olc/crashbox/logkit)+ stop olc2 +
  关 wifi 后台扫描(settings)。pm disable 跨重启持久, 故默认关。
- BgTrim(默认关): 周期(interval_s=90)软回收后台内存(am send-trim-memory): 重度社交/流媒体 BACKGROUND,
  息屏再回收相册/音乐工作集 COMPLETE; 跳过前台、绝不杀进程(faithful ASB)。
- 不移植音频/相机 vendor 改写。README/changelog 同步。CMake host 构建链接通过。

## 260625c
用众包真实频率表(scaling_available_frequencies)校正 per-SoC 配置 + FSM 频率地板:
- 修关键 bug: config_8E / config_8G5 是 config_8E5 的复制粘贴, 频率界用了 SM8850 的值 ——
  8G5(SM8845, 大核真实 max 3436800)被设成 fast c1 4608000(4.6GHz!), 8E(SM8750)同样超界。
  按各 SoC 真实可用档位 吸附+钳制 修正(8E 15 处 / 8G5 13 处 / 8E5 3 处 / 8G2 5 处; 8G3 已正确 0 改)。
  例: 8G5 fast c1 4608000→3436800; 8E c1 min 768000→1017600(8E 大核真实地板)。
- 数据质量注: 问卷只采 cpu0+cpu6, 仅 6+2 布局(8E5/8E/8G5)的 cpu6=超大核可信; 8G3/8G2 是
  1+x+x, cpu6≠配置的 c1(policy2/3), 故只校正其 c0, c1 不动(待补 cpu7 数据)。
- FSM 频率缩放地板改读真实 cpuinfo_min_freq(原固定 300MHz), 天花板仍读 cpuinfo_max_freq;
  两端都跟随本机真实频率表。CMake host 构建链接通过。

## 260625b
FSM 频率界改为读频率表自适配(移植 ASB asb_bounds_scale): 运行时读各簇 cpuinfo_max_freq,
按 界×本机max/参考max(SM8650: 小核2035200/大核3302400)缩放, 钳到 [300MHz, 本机max];
读不到则回退原值。不再写死 SM8850, 任意 SoC 跑 auto+FSM 频率界都跟随本机硅片。CMake 构建通过。

## 260625
移植 ASB FSM 核心, 仅在 mode=auto 启用(其他模式完全不受影响, 保持 governor 自主):
- 新增 include/AutoFsm.hpp(移植 asb_fsm.h / asb_fsm_bounds.generated.h, MIT): 六状态
  (DEEP_IDLE/LIGHT_IDLE/MODERATE/HEAVY/SUSTAINED/GAMING)、目标判定(GPU≥65→GAMING 连续6确认/HEAVY,
  GPU≥35或load≥20→HEAVY, load≥14或屏亮GPU≥12→MODERATE, 否则 LIGHT_IDLE; 息屏→DEEP_IDLE)、
  升/降迟滞(4s/10s, 热入 SUSTAINED 即时)、最小驻留(HEAVY 8s/GAMING 25s/SUSTAINED 24s)、
  SUSTAINED 温控(57℃ 进/49℃ 出 + 180s 时间逃逸)、频率界 lerp(floor,ceil,level) 并由 Smart alpha
  在 battery↔balanced 包络间混合。频率界常量取自 generated.h(SM8850 小核/大核)。
- auto 线程: FSM 子模式每 2s 一 tick 直接写各簇频率上下限; 只在【无 App 画像】时驱动, 有画像即让位;
  governor 沿用预置基础档(只改频率界 → 簇内仍 walt 自主调频, 不违背 Kin 理念)。
- enforceState 在 auto+FSM 无画像时让位(FSM 自重写, 避免与静态基础档打架)。
- config 新增 Function.AutoMode.fsm_enable(默认 true; false=退回按 alpha 选整档)。
- 【仍不移植】FSM 不在 powersave/balance/performance/fast 下运行 —— 那些模式保持纯 Kin。
CMake host 构建链接通过。

## 260624d
评估 ASB FSM 可移植性并移植"支撑件"(只读指标, 不触碰频率控制, 不与 governor 自主调频冲突):
- 温度尖峰守卫(移植 asb_metrics.h spike guard): 单 tick 跳升≥25℃ 且次高传感器不背书(孤立离群)
  判为假尖峰, 保持上次好值且不推进基线 —— 防单个抽风传感器误触热否决/污染学习。
- GPU 占用读取(移植 metrics_read_gpu): GPU≥65%(ASB GAMING 阈值)计为重载 tick, 让会话
  sustained_heavy 判定更准(原仅靠暖 tick 近似)。
- 【未移植·有意】FSM 六状态核心(每 2s 按负载/GPU/温度重写 scaling_max_freq)= Kin 当初移除的
  场景识别同类, 移了会重新引入"CPU 粘高频不降频", 故不移植。
说明见 AutoMode.hpp 注释。CMake host 构建链接通过。

## 260624c
拿到 ASB 真实源码后逐项核对自动模式, 修正照"摘要"实现时的几处偏差(全部对照 asb_smart.h):
- 桶编号改 ASB 真式 bucket_id = daypart*2 + weekend(反周末 = bucket^1)。
- 会话学习步长修正: dt_alpha 不再按时长/信任加权、不再 ±50 封顶; 改为 ASB 原式
  dt_alpha × 学习率(LEARN_RATE_X1000=80)/1000; 时长权×信任权仅累加进置信度 eff_obs(封顶 4000)。
- 补 thermal_hit 信号(会话触 veto 温度 +100); drainy 需亮屏≥600s(DRAIN_MIN_ON_SEC)。
- 分级回退阈值改 CONF_LOW=350(原误用 200), 并补"弱 EXACT"回退路径。
- 夜间安全覆盖改按时段{睡,深夜,醒}判定(原误用时钟 0-6)。
- 低电覆盖加迟滞闩(≤20% 未充电触发, ≥40% 或充电解除; 临界 ≤10%)。
- config 项 learn_step→learn_rate(默认 80)。CMake host 构建链接通过。

## 260624b
自动模式: 按 AutoSystemBoost(ASB) Smart Mode 真实源码(asb_smart_defs.h / asb_smart.h,
MIT, Dima Krylov)忠实重写学习/决策, 修正此前照 README 实现与真算法的出入:
- 时段边界改用真值: 睡0-6 醒6-9 上午9-12 白天12-17 傍晚17-21 深夜21-24(原 12-18/18-22/22-24)。
- 电量阈值改 ASB 真值: 低电 20%、临界 10%(原 25/15); 覆盖改为强制 alpha(低电≥800/临界≥900)。
- 热否决 65℃(原 58), 并加 50→65℃ 软提升曲线; 否决/夜间/低电改为"上抬 alpha"与学习叠加。
- 引入置信度(每桶 obs 累计, ~8 会话满信, 7/37 天衰减)+ 置信门控有效缩放(低置信向中性 500 收敛)。
- 分级回退: 精确桶→反周末同时段→同时段类→全局最佳→中性。
- 会话学习改 方向信号 × 时长权(25/50/100/125) × 信任权(CLEAN100/PARTIAL40/NOISY15/DIRTY0),
  单会话封顶 5%。状态文件升级为 b<i>=alpha,obs,last_seen, 并写 alpha_raw/conf 到 auto_status.txt。
- 移植实现见 include/AutoMode.hpp 顶部 MIT 署名(MIT→GPL-3.0 兼容)。

## 260624
新增:自动模式(mode=auto) —— 借鉴 AutoSystemBoost 的 Smart Mode 思路, 但只在
powersave/balance/performance 三个【整模式】之间切换(分钟级, 不重写频率上限、不做场景识别),
不与"governor 自主调频"理念冲突, 不会重蹈频繁改 scaling_max_freq 导致不降频的坑。
- 决策输入:CPU 温度 / 电量 / 充电态 / 屏幕亮灭 / 时段(12 桶=6 时段×工作日周末)。
- 自学习:每个亮屏会话结束后, 按是否过热/高耗电把当前时段桶的 alpha 朝省电或均衡平移 5%
  (对齐 ASB);脏数据(会话过短/充电中)不学习。状态存 auto_state.txt, 重启不丢。
- 硬否决:临界低电/超温/夜间息屏 → 直接 powersave;偏热或低电 → 禁止 performance。
- App 画像仍优先:auto 只决定"无专属画像时"的基础档, 游戏等画像照常接管。
- 全部阈值可在 config.json 的 Function.AutoMode 覆盖, 无需重编译;实时状态写 auto_status.txt。
- 切换方式:`echo auto > /sdcard/Android/Kin/mode.txt`。

## 260615
修复:游戏档常驻 cpuset 矫正器(对抗 ORMS 收窄)之前会在前台 cpuset 被收成任意非全核值
时都拉回 0-7。现统一为仅当被收窄到 0-5(丢大核)或 2-7(丢小核)才拉回,其它取值不动。
(去掉上一版在画像应用处新增的重复机制,改为单一矫正器,逻辑一致。)
游戏档 cpuset:不再无条件把前台 cpuset 写成 0-7。仅当 top-app/foreground 当前被限到
0-5(丢大核)或 2-7(丢小核)时才拉回 0-7,其它取值(含已是 0-7、ROM/其它工具自定义)
一律不动,避免抢占。
游戏画像(game_heavy):去掉各簇人为的 1200MHz 频率下限,改为跟随该机型默认(balance)
下限。修复王者等轻载游戏大核被钉在 1200MHz 的问题;上限不变,原神/PUBG 等重载满载
仍可拉满。已同步全部 SoC 配置。
Kin / Kin-APP:修复黑名单(不接管)应用未跟随全局、被卡在省电模式的问题。
现黑名单及无匹配前台均跟随全局模式(perapp "*" 全局默认行),不再强制省电;
如需省电可显式设置全局为省电模式。

## 260614
Kin:
去除向APP的数据传输，减少额外开销
优化前台检测问题，不依靠app监测前台也可以实现前台检测，但依然有点小问题，如果影响使用则需app监测
Kin-APP:不依赖Kin获得真实数据，去除无用功能。解决在省电模式下无法修改大核参数问题。