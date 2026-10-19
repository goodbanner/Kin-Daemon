# Kin (OnePlus V Pro) — Code Wiki

> 本文档由对仓库源码的结构化分析整理而成，目标是让阅读者在不逐行阅读代码的前提下，快速建立对项目整体架构、模块职责、关键类与函数、依赖关系与运行方式的完整心智模型。
>
> - 仓库主体：`Kinetic` —— 一个运行于 Android 用户态的 CPU/系统调度守护进程（Magisk 模块）
> - 配套：卡密授权服务器（**独立私有仓库，不随本仓库分发**）、可选 eBPF 后端（**未随仓库分发**）、配套 App（`magisk/Kin.apk`）
> - 模块标识：`id=Kin`，对外名 `OnePlus V Pro`，当前版本 `Ⅴ⁰⁵`（[module.prop](magisk/module.prop)）

---

## 目录

1. [项目概述](#1-项目概述)
2. [整体架构](#2-整体架构)
3. [目录结构](#3-目录结构)
4. [主要模块职责](#4-主要模块职责)
5. [关键类与函数说明](#5-关键类与函数说明)
6. [依赖关系](#6-依赖关系)
7. [项目运行方式](#7-项目运行方式)
8. [关键数据流与状态机](#8-关键数据流与状态机)
9. [配置体系](#9-配置体系)
10. [构建与发布](#10-构建与发布)
11. [附录：术语表](#11-附录术语表)

---

## 1. 项目概述

### 1.1 一句话定位

**Kinetic** 是一个面向高通骁龙 / 联发科天玑旗舰 SoC 的 Android **用户态系统调优守护进程**，通过 Magisk/KernelSU 在开机阶段以 root 域拉起，常驻后台对 CPU 调频、cpuset、CFS/RT 调度、GPU、内存回收、网络、热限制等进行持续治理；并内置一个移植自 **AutoSystemBoost (ASB)** 的「智能模式」自学习状态机，能根据用户使用习惯自动在六档状态间切换。

### 1.2 核心能力

| 能力域 | 说明 |
| --- | --- |
| **五模式手动调度** | powersave / balance / performance / fast / auto，每模式对应一组 CPU 频率界、cpuset、CFS 参数 |
| **AutoMode 自学习** | 6 时段 × 工作日/周末共 12 桶，ε-greedy 探索 + 奖励闭环 + 置信门控 + 分级回退 |
| **6 状态 FSM** | DEEP_IDLE / LIGHT_IDLE / MODERATE / HEAVY / SUSTAINED / GAMING，带迟滞与最小驻留 |
| **帧感知调度 FAS** | GAMING 态下 P 控制器闭环，依据实测帧率动态收紧/放开大核频率界 |
| **anti-clamp 对抗** | 检测厂商 msm_performance 压频并主动反写，绕过 OPlus 隐性限频 |
| **触控加速** | TouchBoost：屏幕触控事件触发瞬时提频 |
| **线程绑核 / RT 调度** | 按包名+线程名规则下发 `sched_setaffinity` / `SCHED_FIFO/RR` |
| **应用画像** | 9 类 CatPolicy（game/audio/video/...）+ perapp 覆盖层 |
| **SysTune** | VM/TCP/WiFi sysctl 三档调优，BootTrim 停 oplus 后台服务，BgTrim 触发 `am send-trim-memory` |
| **Metrics 共享内存** | 抽象 socket `@kin_metrics` + memfd_create 零拷贝，供 App 实时读取守护状态 |
| **卡密授权** | Ed25519 签名令牌，设备绑定，支持月/季/年/永久四档套餐 + 易支付/支付宝 |

### 1.3 设计哲学

- **零信任厂商节点**：OPlus/MTK 的 horae、game_opt、msm_performance 等节点会被 bind-mount 锁定或主动反写
- **最大努力回退**：SELinux 规则、共享内存、eBPF、GPU 路径探测全部「能上则上，不能上自动退到文本/socket/默认」
- **移植优先**：AutoMode/FSM/FAS 三件套均从 ASB 移植，频率界常量取自 `asb_fsm_bounds.generated.h` 并按本机 `cpuinfo_max_freq` 缩放
- **守护轻量、App 重表达**：守护只做治理与状态上报，所有 UI/卡密交互/远程命令由外置 `Kin.apk` 完成，二者通过 sysfs 文件 + 抽象 socket 解耦

---

## 2. 整体架构

### 2.1 分层架构图

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Kin.apk (外置, 仓库外)                       │
│   UI / 卡密激活 / 模式切换 / 远程命令接收 / Metrics 可视化          │
└───────────────▲───────────────────────────────▲─────────────────────┘
                │ 文件接口                       │ @kin_metrics socket
                │ mode.txt / auth.json           │ + memfd 零拷贝
┌───────────────┴───────────────────────────────┴─────────────────────┐
│                     Kinetic 守护进程 (本仓库)                       │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ ┌──────────┐ │
│  │  Schedule    │  │ AutoModeCtl  │  │  Function    │ │ Metrics  │ │
│  │  (中央协调)  │◄─┤ (自学习)     │  │  (节点写入)  │ │ Server   │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘ └──────────┘ │
│         │                 │                 │                       │
│  ┌──────▼───────┐  ┌──────▼───────┐  ┌──────▼───────┐ ┌──────────┐ │
│  │  AutoFsm     │  │  AutoFas     │  │  SysTweaks   │ │ Event    │ │
│  │  (6态FSM)    │  │  (帧闭环)    │  │  (VM/网络)   │ │ Monitor  │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │ (proc    │ │
│                                                     │  connector│ │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ └──────────┘ │
│  │  Config      │  │  JsonConfig  │  │  ThreadRules │ ┌──────────┐ │
│  │  (全局配置)  │◄─┤ (热重载解析) │  │  (绑核规则)  │ │ License  │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │ (卡密)   │ │
│                                                     └──────────┘ │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ ┌──────────┐ │
│  │  Soc         │  │  Logger      │  │  Utils       │ │ tweetnacl│ │
│  │  (平台适配)  │  │  (落盘日志)  │  │  (系统工具)  │ │ (Ed25519)│ │
│  └──────────────┘  └──────────────┘  └──────────────┘ └──────────┘ │
└───────────────▼─────────────────────────────────────────────────────┘
                │ sysfs / proc / cpuset / sched / netlink
┌───────────────▼─────────────────────────────────────────────────────┐
│                       Android Linux Kernel                          │
│   cpufreq / sched / cpuset / msm_performance / thermal / netlink    │
└─────────────────────────────────────────────────────────────────────┘
                ▲
┌───────────────┴─────────────────────────────────────────────────────┐
│        授权服务 (Express.js, 独立私有仓库, 不随本仓库分发)          │
│   卡密生成/验证 / 设备绑定 / 远程命令下发 / 支付回调 / 后台管理     │
│   libSQL(Turso) ── products/license_keys/orders/admins/...          │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 进程视图

| 进程 | 启动方式 | 权限域 | 职责 |
| --- | --- | --- | --- |
| `Kinetic` | [magisk/service.sh](magisk/service.sh) 每 2s 拉起 | magisk/su/ksu | 调度守护、状态上报、卡密读取 |
| `Kin.apk` | 用户启动 App | untrusted_app | UI、卡密激活、模式切换、Metrics 消费 |
| `license-server` | Docker / Vercel(外部) | node:20 | 卡密服务、支付、远程命令 |

### 2.3 守护线程模型

`Schedule` 类启动 **9 个后台线程**（见 [include/scheduler.hpp](include/scheduler.hpp)）：

| 线程 | 周期/触发 | 职责 |
| --- | --- | --- |
| `fileWatchTask` | inotify / 2s | 监听 mode.txt、config.json、auth.json 变化，触发热重载 |
| `appProfileTask` | 1~2s | 读取 top-app，匹配 AppProfile，下发 perapp 覆盖层 |
| `bootResettleTask` | 一次性 + 延迟 | 开机后等系统稳定再二次应用基础模式（避开 OPlus 抢节点） |
| `cpuEnforceTask` | 1~2s | 周期性 enforce CPU 频率界、cpuset，防止被厂商回写 |
| `autoModeTask` | 5min | AutoMode 12 桶学习、FSM 状态评估、奖励闭环 |
| `bgTrimTask` | 30min/事件 | 后台应用内存回收 `am send-trim-memory` |
| `threadAllocTask` | 事件 | 按 ThreadRules 下发 sched_setaffinity / RT 优先级 |
| `eventMonitorTask` | 事件 | proc connector 事件循环（fork/exec） |
| `powerRecorderTask` | 30s | 记录功耗/温度样本到 AI 目录供学习 |

主线程仅 `pause()` 等待 SIGTERM/SIGINT/SIGPIPE 信号；用 `eventfd` 实现可中断等待（`kRecheckSeconds=5min`）。

---

## 3. 目录结构

```
/workspace
├── src/
│   ├── main.cpp                 # 守护进程入口
│   ├── tweetnacl.c              # Ed25519 加密实现（公有领域, 独立 C 编译单元）
│   └── bpf/
│       ├── monitor.bpf.c        # 可选 eBPF 后端
│       └── README.md            # proc connector vs eBPF 说明
├── include/                     # 全部头文件（header-only 类）
│   ├── scheduler.hpp            # 中央协调器 Schedule (2467 行)
│   ├── AutoMode.hpp             # 自学习 AutoModeCtl (1650 行)
│   ├── AutoFsm.hpp              # 6 状态 FSM (335 行)
│   ├── AutoFas.hpp              # 帧感知调度 (358 行)
│   ├── Config.hpp               # 全局配置中心 (377 行)
│   ├── JsonConfig.hpp           # config.json 解析 (727 行)
│   ├── Function.hpp             # 节点写入封装 (921 行)
│   ├── SysTweaks.hpp            # VM/网络/Trim 调优 (247 行)
│   ├── EventMonitor.hpp         # proc connector 后端 (98 行)
│   ├── ThreadRules.hpp          # 线程绑核规则 (241 行)
│   ├── ThreadPriorityRules.hpp  # 线程 RT 优先级规则 (212 行)
│   ├── MetricsServer.hpp        # 共享内存 Metrics (329 行)
│   ├── License.hpp              # 卡密读取 (90 行)
│   ├── Logger.hpp               # 落盘日志 (205 行)
│   ├── Soc.hpp                  # 平台适配 (74 行)
│   ├── Utils.hpp                # 系统工具 (441 行)
│   ├── LibUtils.hpp             # Fastatoi/FastSnprintf (110 行)
│   └── tweetnacl.h              # NaCl C 声明
├── magisk/                      # Magisk 模块安装包
│   ├── module.prop              # 模块元信息
│   ├── customize.sh             # 安装脚本 (386 行, SoC 识别/部署/APK 安装)
│   ├── service.sh               # 开机启动 (210 行)
│   ├── post-fs-data.sh          # 早期节点锁定 (95 行)
│   ├── action.sh                # 动作面板 (182 行)
│   ├── system.prop              # 系统属性覆写 (109 行)
│   ├── sepolicy.rule            # SELinux 放行
│   └── vtools/
│       ├── init_vtools.sh       # 部署 powercfg
│       ├── powercfg.sh          # 模式切换脚本
│       └── powercfg.json        # 模式映射
├── Json/                        # 每 SoC 配置文件
│   ├── config_8E.json           # 骁龙 8 Elite (sm8750)
│   ├── config_8E5.json          # 骁龙 8 Elite Gen 5 (sm8850)
│   ├── config_8G1.json ~ 8G5    # 骁龙 8 Gen1~5
│   ├── config_7P3.json          # 骁龙 7+ Gen3 (sm7675)
│   ├── config_D9300.json        # 天玑 9300 (mt6989)
│   ├── config_D9400.json        # 天玑 9400 (mt6991)
│   └── perapp_powermode.txt     # 预填应用画像表
├── tests/                       # 单元测试 (31 个: FSM/自学习/节点写入/配置校验/前台缓存…)
├── CMakeLists.txt               # CMake 构建 (项目名 Kin VERSION 3.2)
├── build.sh                     # bash 构建脚本
├── build_pack.py                # Python 一键构建+打包
├── README.md                    # 项目说明
├── changelog.md                 # 变更日志
├── update.json                  # 模块更新元信息
└── .github/workflows/build.yml  # CI 双版本构建
```

---

## 4. 主要模块职责

### 4.1 守护进程核心模块

| 模块 | 头文件 | 职责 |
| --- | --- | --- |
| **入口** | [src/main.cpp](src/main.cpp) | 信号处理、`g_paid` 全局、eventfd 可中断等待、构造 `Schedule` |
| **中央协调器** | [include/scheduler.hpp](include/scheduler.hpp) | `Schedule` 类，启动 9 线程，统一调度所有子系统 |
| **AutoMode 自学习** | [include/AutoMode.hpp](include/AutoMode.hpp) | 12 桶学习、置信门控、分级回退、ε-greedy 探索 |
| **6 状态 FSM** | [include/AutoFsm.hpp](include/AutoFsm.hpp) | 状态评估、频率界计算、迟滞、最小驻留 |
| **帧感知调度** | [include/AutoFas.hpp](include/AutoFas.hpp) | GAMING 态 P 控制器闭环帧率 |
| **配置中心** | [include/Config.hpp](include/Config.hpp) | 14 子命名空间全局配置、CatPolicy、AppProfile |
| **JSON 解析** | [include/JsonConfig.hpp](include/JsonConfig.hpp) | config.json 热重载解析 |
| **节点写入** | [include/Function.hpp](include/Function.hpp) | cpuset/GPU/CFS/FAS 节点写入封装 |
| **系统调优** | [include/SysTweaks.hpp](include/SysTweaks.hpp) | VM/TCP/WiFi/BootTrim/BgTrim |
| **事件监控** | [include/EventMonitor.hpp](include/EventMonitor.hpp) | proc connector fork/exec 事件 |
| **线程规则** | [include/ThreadRules.hpp](include/ThreadRules.hpp) + [ThreadPriorityRules.hpp](include/ThreadPriorityRules.hpp) | 绑核 + RT 优先级规则匹配 |
| **Metrics 上报** | [include/MetricsServer.hpp](include/MetricsServer.hpp) | 抽象 socket + memfd 零拷贝 |
| **卡密** | [include/License.hpp](include/License.hpp) | 读取 auth.json，返回付费状态 |
| **日志** | [include/Logger.hpp](include/Logger.hpp) | 落盘 /sdcard/Android/Kin/log.txt，512KB 轮转 |
| **平台适配** | [include/Soc.hpp](include/Soc.hpp) | sysfs 路径候选、频率基准、热优先级 |
| **工具** | [include/Utils.hpp](include/Utils.hpp) | 文件写、top-app 读取、dumpsys 解析 |
| **加密** | [include/tweetnacl.h](include/tweetnacl.h) + [src/tweetnacl.c](src/tweetnacl.c) | Ed25519 签名（公有领域实现） |

### 4.2 Magisk 模块

| 文件 | 时机 | 职责 |
| --- | --- | --- |
| [customize.sh](magisk/customize.sh) | 安装时 | SoC 识别、清理 legacy 模块、部署 config.json + Kinetic、安装 Kin.apk、热更新二进制 |
| [post-fs-data.sh](magisk/post-fs-data.sh) | 早期启动 | bind-mount 锁定 OPlus 节点、关 oplus_frame_boost、SLC XML 部署 |
| [service.sh](magisk/service.sh) | boot_completed 后 | 等 boot/login、拉起 Kinetic、mode.txt 合法性、cpuset 兜底、写 status |
| [action.sh](magisk/action.sh) | 用户点击 | 显示状态、挂载健康检查、守护兜底重启 |
| [system.prop](magisk/system.prop) | 系统启动 | 墓碑冻结、VSync、日志关闭、热限关闭、Vulkan safe mode、LMK 阈值、高刷解禁 |
| [sepolicy.rule](magisk/sepolicy.rule) | 系统启动 | 共享内存零拷贝 SELinux 放行 |
| [vtools/init_vtools.sh](magisk/vtools/init_vtools.sh) | 安装时 | 部署 powercfg.sh/json、初始化 mode.txt |

### 4.3 卡密授权服务（外部组件）

授权服务（Express.js + libSQL/Turso）负责卡密生成/验证/设备绑定/解绑、JWT 后台、支付回调与远程命令下发。
它**不在本仓库内**，也不随本仓库分发；本仓库只包含消费方逻辑：

| 文件 | 职责 |
| --- | --- |
| [include/License.hpp](include/License.hpp) | 客户端验签：Ed25519 公钥校验服务端签发的短时令牌 |
| [src/tweetnacl.c](src/tweetnacl.c) | Ed25519 验签实现（NaCl） |

### 4.4 可选 eBPF 后端（未随仓库分发）

前台/线程采集默认走 proc connector；真·eBPF 后端（tracepoint `sched_process_fork`/`exec`、
`sys_enter_sched_setaffinity`，perf_event_array 上报）为可选组件，**未包含在本仓库中**，
需自行实现并接入构建。

---

## 5. 关键类与函数说明

### 5.1 `Schedule`（中央协调器）

**位置**：[include/scheduler.hpp](include/scheduler.hpp)（2467 行）

`Schedule` 是整个守护进程的中枢，所有子系统的入口都由它驱动。

#### 关键成员

| 成员 | 类型 | 说明 |
| --- | --- | --- |
| `applyMtx` | `std::recursive_mutex` | 全局应用锁，所有写节点操作串行化 |
| `g_paid` | `std::atomic<bool>` | 全局付费状态（main.cpp 中由 License 提供） |
| `recheckFd` | `int` | eventfd，用于中断 5min 等待 |

#### 关键方法

| 方法 | 说明 |
| --- | --- |
| `applyCurrentMode()` | 应用当前模式（手动或 Auto 评估结果）的总入口 |
| `applyBaseMode(mode)` | 应用基础模式（powersave/balance/...）的频率界 + cpuset |
| `applyAppProfile(pkg)` | 应用 per-app 画像覆盖层 |
| `primeAutoFsm()` | 初始化 AutoMode FSM（开机或切换到 auto 时） |
| `writeFsmCaps(caps)` | 把 FSM 计算出的频率界写到 sysfs |
| `applyAntiClampNode()` | anti-clamp 反写 msm_performance |
| `FreqWriter(path, val)` | 统一的 sysfs 频率写入（处理 O_NONBLOCK 限制） |
| `onlineAll()` | 全核 online |
| `reconcileAutoCores()` | 协调 AutoMode 决策的核数与实际 online 状态 |

#### 9 个后台线程任务方法

`fileWatchTask` / `appProfileTask` / `bootResettleTask` / `cpuEnforceTask` / `autoModeTask` / `bgTrimTask` / `threadAllocTask` / `eventMonitorTask` / `powerRecorderTask` —— 见 [§2.3](#23-守护线程模型)。

### 5.2 `AutoModeCtl`（自学习）

**位置**：[include/AutoMode.hpp](include/AutoMode.hpp)（1650 行）

移植自 ASB Smart Mode。

#### 12 桶学习结构

每个桶维护：
- `alphaRaw` —— 学习到的"激进程度"原值
- `obsX100` —— 观察次数 ×100（用于置信度）
- `dtMean` / `dtMad` —— 负载均值与平均绝对偏差

6 时段 × 工作日/周末 = 12 桶。

#### 关键常量

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `ASB_CONF_LOW` | 350 | 置信度下限（低于则回退） |
| `ASB_CONF_HIGH` | 650 | 置信度上限（高于才采信本桶） |
| `kStateDir` | `/sdcard/Android/Kin/.kin` | 状态持久化目录 |
| `kAiDir` | `.../Kin/AI` | 学习样本目录 |

#### 分级回退链

```
EXACT → DAYPART_ONLY → CLASS → GLOBAL → SAFE
```

当某桶置信度不足时，逐级放宽到更大范围的数据。

#### 覆盖层（否决机制）

- **夜间覆盖**：特定时段强制降级
- **低电覆盖**：电量低于阈值时压低
- **热否决**：skin thermal 超阈值时压低
- **anti-clamp 覆盖**：检测到厂商压频时主动反制

#### 探索闭环

ε-greedy：以小概率 ε 探索新档位，观察奖励（续航 + 流畅度综合），更新对应桶的 alpha。

#### 持久化

`auto_state.txt` —— 12 桶状态序列化，开机恢复。

### 5.3 `AutoFsm`（6 状态 FSM）

**位置**：[include/AutoFsm.hpp](include/AutoFsm.hpp)（335 行）

#### 6 状态

```
DEEP_IDLE → LIGHT_IDLE → MODERATE → HEAVY → SUSTAINED → GAMING
   (0)         (0.15)      (0.45)    (0.72)   (0.62)     (1.0)
```

> 注：`SUSTAINED=0.62` 比 `HEAVY=0.72` 低，是真正的"持续压频"档，用于长时间高负载防热降频。

#### Caps 结构

```cpp
struct Caps {
    int minL, maxL;   // 小核频率界
    int minB, maxB;   // 大核频率界
    int state;        // 当前状态枚举
};
```

#### `computeCaps(alpha)`

用 `lerp` + `blend(bal, bat, alpha)` 计算频率界。`BAT_*` / `BAL_*` 常量取自 `asb_fsm_bounds.generated.h`，按本机 `cpuinfo_max_freq` 缩放（移植 `asb_bounds_scale`）。

#### 迟滞与驻留

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `UP_WINDOW` | 2 | 升档需要连续 2 次样本达标 |
| `DOWN_WINDOW` | 5 | 降档需要连续 5 次样本达标（更保守） |
| `DWELL_GAMING` | 25s | GAMING 态最小驻留 25s，避免抖动 |

### 5.4 `AutoFas`（帧感知调度）

**位置**：[include/AutoFas.hpp](include/AutoFas.hpp)（358 行）

仅在 GAMING 态启用，闭环控制大核频率界以维持目标帧率。

#### 输出结构

```cpp
struct Out {
    bool engaged;   // 是否介入
    int  maxL, maxB; // 调整后的频率界
};
```

#### P 控制器

- `KP = 3e-4` —— 比例系数
- `TIERS[] = {30, 60, 90, 120, 144, 165}` —— 帧率档位

#### 帧源

1. **主**：`Soc::kMeasuredFps` sysfs 节点
2. **兜底**：`dumpsys SurfaceFlinger --latency`，频率 ≤1Hz

### 5.5 `Config`（全局配置中心）

**位置**：[include/Config.hpp](include/Config.hpp)（377 行）

#### 14 个子命名空间

`Meta` / `Policy` / `Cpuset` / `LaunchBoost` / `OfficialMode` / `SysTune` / `BootTrim` / `BgTrim` / `TouchBoost` / `ThreadAlloc` / `AutoMode` / `Scheduler` / `GpuFreq` / `Performances`

#### 关键常量

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `kClusterCount` | 4 | CPU 集群数 |
| `kCoreCount` | 8 | CPU 核数 |
| `kMaxSchedParams` | 16 | 最大调度参数条目 |

#### CatPolicy（9 类应用画像）

`game` / `audio` / `video` / `image` / `social` / `news` / `maps` / `productivity` / `undefined`

`catPolicyTable` + `catPolicyDefault` —— `JsonConfig::resetState` 热重载时复位。

#### AppProfileModel + `matchPackage`

迭代式通配符匹配（支持 `?` `*`），用于 perapp 覆盖层。

### 5.6 `JsonConfig`（配置解析）

**位置**：[include/JsonConfig.hpp](include/JsonConfig.hpp)（727 行）

#### `readConfig()`

解析 `config.json` → 填充 `Config::` 各字段。

#### `parseFunctionSection`

逐子节解析：`Cpuset` / `LaunchBoost` / `OfficialMode` / `SysTune` / `BootTrim` / `BgTrim` / `TouchBoost` / `ThreadAlloc` / `Scheduler` / `GpuFreq` / `AppProfile` / `AutoMode`

#### `resetState`

热重载时复位 `catPolicyTable` 到 `catPolicyDefault`，避免旧规则残留。

### 5.7 `Function`（节点写入封装）

**位置**：[include/Function.hpp](include/Function.hpp)（921 行）

#### 四类功能

| 方法 | 说明 |
| --- | --- |
| `cpusetFunction` | 写 cpuset cgroup.procs / cpus |
| `gpuFreqControl` | GPU 频率控制（Mali devfreq → qcom kgsl → pwrlevel 兜底） |
| `CfsSchedOpt` | CFS 调度参数（up/down_rate_limit_us、hispeed_load 等） |
| `FeasFunc` | FAS 帧感知相关节点 |

#### GPU 路径级联探测

```
Mali devfreq  →  qcom kgsl  →  pwrlevel 兜底
```

#### `gpuFreqGuard` 自适应轮询

- 1s 快速（活跃时）
- 3s 慢速（空闲时）
- 10s 兜底（节点异常时）

### 5.8 `SysTweaks`（系统调优）

**位置**：[include/SysTweaks.hpp](include/SysTweaks.hpp)（247 行）

移植自 ASB 非 CPU 调优部分。

#### `applyProfileTune(mode)`

三档调优：

| mode | VM | TCP | WiFi |
| --- | --- | --- | --- |
| `battery` | 偏保守 | low_latency 关 | 省电 |
| `balanced` | 默认 | cubic | 默认 |
| `perf` | 偏激进 | bbr/low_latency | 性能 |

#### `bootTrimOnce()`

停掉 OPlus 后台服务：`oplus.midas` / `olc` / `crashbox` / `logkit`

#### `bgTrimTick(top, screenOff)`

调用 `am send-trim-memory` 对后台应用做内存回收。

### 5.9 `EventMonitor`（事件监控）

**位置**：[include/EventMonitor.hpp](include/EventMonitor.hpp)（98 行）

#### 后端

`PF_NETLINK` + `NETLINK_CONNECTOR` + `CN_IDX_PROC`

#### `drain<OnEvent>`

模板方法，回调 `PROC_EVENT_FORK` / `PROC_EVENT_EXEC` 的 `child_tgid`，供 `threadAllocTask` 即时下发绑核。

### 5.10 `ThreadRules` + `ThreadPriorityRules`

**位置**：[include/ThreadRules.hpp](include/ThreadRules.hpp)（241 行）+ [ThreadPriorityRules.hpp](include/ThreadPriorityRules.hpp)（212 行）

#### 规则格式

```
# 绑核
pkg{thread}=cores          # 如 com.tencent.tmgp.sgame{UnityMain}=4-7

# 优先级
pkg{thread}=POLICY PRIO    # 如 com.netease.cloudmusic{ExoPlayer}=SCHED_FIFO 2
```

#### `resolveThread` 优先级

```
精确 > [范围] > ? 单字符 > * 通配 > 进程默认
```

> 注：`ThreadRules` 不直接调用 `sched_setaffinity`，只产出 `cpu_set_t`，由 `Schedule::threadAllocTask` 统一下发。

### 5.11 `MetricsServer`（共享内存上报）

**位置**：[include/MetricsServer.hpp](include/MetricsServer.hpp)（329 行）

#### 抽象 socket

名称 `@kin_metrics`（Linux 抽象命名空间 socket）

#### 两级传输

1. **文本通道**：`key=value\n`，用于少量状态
2. **共享内存零拷贝**：`memfd_create` + `SCM_RIGHTS` 传 fd，8 字节门铃通知更新

#### 握手

客户端连入后发送 `K1\n` 完成握手。

### 5.12 `License`（卡密）

**位置**：[include/License.hpp](include/License.hpp)（90 行）

#### `lic::readAuth()`

读取 `/sdcard/Android/Kin/AI/auth.json`。

#### `Result`

```cpp
struct Result {
    bool ok = true;          // 恒为 true（读取成功）
    bool paid;               // 是否已付费
    std::string reason;      // 未付费原因
    std::string plan;        // 套餐：month/quarter/year/permanent
    int64_t expiresAt;       // 过期时间戳
};
```

> **重要**：守护已**放弃** Ed25519 验签，只读 App 下发的明文状态。Ed25519 验签在 App 端完成（App 从 license-server 获取令牌后本地验签，通过才写 auth.json）。

#### `KIN_NO_LICENSE` 宏

定义时（公益版）`readAuth()` 恒返回已付费，绕过卡密。

### 5.13 `Logger`（日志）

**位置**：[include/Logger.hpp](include/Logger.hpp)（205 行）

- 落盘 `/sdcard/Android/Kin/log.txt`
- 512KB 轮转
- 括号注释剥离
- 中文级别名：调试 / 信息 / 警告 / 错误
- `mutex` 线程安全

### 5.14 `Soc`（平台适配）

**位置**：[include/Soc.hpp](include/Soc.hpp)（74 行）

#### 频率基准

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `REF_HWMAX_LITTLE` | 2035200 | 小核基准最大频率 |
| `REF_HWMAX_BIG` | 3302400 | 大核基准最大频率 |

#### sysfs 路径候选

`kMeasuredFps` / `kGpuBusy` / `kCurrentNow` / `kScreenBacklight` / `kThermalKeywords`

#### 厂商压频节点

`kVendorFreqCap = "/sys/kernel/msm_performance/parameters/cpu_max_freq"`

#### `skinThermalPriority`

五级热优先级，用于热否决覆盖层。

### 5.15 `Utils`（系统工具）

**位置**：[include/Utils.hpp](include/Utils.hpp)（441 行）

| 方法 | 说明 |
| --- | --- |
| `FileWrite` / `FileWriteBlocking` | sysfs 写入。**注意**：`O_NONBLOCK` 不能用于 governor 节点，故提供 Blocking 版本 |
| `getTopAppFast` | 读 `/dev/cpuset/top-app/cgroup.procs`（比 dumpsys 快） |
| `ForegroundSnapshot` + `parseForegroundOnce` | `dumpsys activity activities` 解析前台包名 |

### 5.16 `LibUtils`

**位置**：[include/LibUtils.hpp](include/LibUtils.hpp)（110 行）

`Fastatoi` / `FastSnprintf` —— 极简 `vsnprintf`，只支持 `%d` / `%s` / `%%`，避免 libc `snprintf` 在热路径上的开销。

### 5.17 `main` 入口

**位置**：[src/main.cpp](src/main.cpp)

- 设置 `SIGTERM` / `SIGINT` / `SIGPIPE` 信号处理
- `g_paid` 全局原子读取 `auth.json`
- `eventfd` 实现可中断等待（`kRecheckSeconds=5min`）
- 启动 `MetricsServer`
- 构造 `Schedule`
- 主线程 `pause()` 等信号

---

## 6. 依赖关系

### 6.1 守护进程内部依赖图

```
                    main.cpp
                       │
                       ▼
                   Schedule ◄────── License (g_paid)
                  (scheduler.hpp)
            ┌──────┬──────┬──────┬──────┬──────┬──────┐
            ▼      ▼      ▼      ▼      ▼      ▼      ▼
        AutoMode AutoFsm AutoFas Function SysTweaks EventMonitor MetricsServer
            │      │      │      │        │         │              │
            └──────┴──────┴──┬───┴────────┴─────────┘              │
                             ▼                                      │
                          Config ◄── JsonConfig                     │
                             │                                      │
                             ▼                                      │
                            Soc ◄── Utils ◄── LibUtils              │
                             │                                      │
                             ▼                                      │
                          Logger ◄──────────────────────────────────┘
                          tweetnacl (License/Ed25519, 可选)
```

### 6.2 外部依赖

#### 守护进程（C++23 / C99）

| 依赖 | 用途 | 是否必需 |
| --- | --- | --- |
| Android NDK r26d | 交叉编译 toolchain | 必需 |
| CMake ≥ 3.18 | 构建系统 | 必需 |
| Ninja | 构建执行器 | 必需 |
| BPF Type Format (BTF) | eBPF CO-RE | 可选（内核需支持） |
| libc (Android bionic) | 标准库 | 必需 |
| libm | 数学 | 必需 |
| liblog | Android log（可选） | 可选 |

> **无第三方 C++ 库依赖**：所有加密（tweetnacl）、JSON 解析（自写）、日志、socket 均为自实现，保证二进制极小且静态链接。

#### License Server（Node.js）

| 依赖 | 用途 |
| --- | --- |
| express | Web 框架 |
| @libsql/client | libSQL/Turso 数据库 |
| jsonwebtoken | JWT 后台登录 |
| tweetnacl (npm) | Ed25519 令牌签发与验证 |
| axios | 支付回调 HTTP |
| multer | 订单图片上传 |

#### Magisk 模块

| 依赖 | 用途 |
| --- | --- | --- |
| Magisk 或 KernelSU | 模块框架、root 域、sepolicy 注入 |
| busybox (Magisk 自带) | shell 工具 |
| Android `am` / `pm` / `dumpsys` | 应用管理 |

### 6.3 运行时数据依赖

| 路径 | 读写方 | 说明 |
| --- | --- | --- |
| `/sdcard/Android/Kin/mode.txt` | App 写 / 守护读 | 当前模式（powersave/balance/performance/fast/auto） |
| `/sdcard/Android/Kin/config.json` | App 写 / 守护读 | 热重载配置（覆盖每 SoC 默认） |
| `/sdcard/Android/Kin/AI/auth.json` | App 写 / 守护读 | 卡密付费状态 |
| `/sdcard/Android/Kin/.kin/auto_state.txt` | 守护读写 | AutoMode 12 桶持久化 |
| `/sdcard/Android/Kin/AI/` | 守护写 | 功耗/温度样本 |
| `/sdcard/Android/Kin/log.txt` | 守护写 | 日志 |
| `/data/powercfg.sh` + `/data/powercfg.json` | vtools 部署 / 守护用 | 模式映射 |
| `@kin_metrics`（抽象 socket） | 守护 listen / App connect | Metrics 实时上报 |
| `/sys/class/...` 各 sysfs | 守护写 | cpufreq/cpuset/... |

---

## 7. 项目运行方式

### 7.1 用户视角：安装与启动

#### 安装流程（[customize.sh](magisk/customize.sh)）

1. **SoC 识别** `identify_chip`：根据 `/proc/cpuinfo` 匹配 SoC（sm8750→8E / sm8850→8E5 / sm8650→8G3 / sm8550→8G2 / sm8450→8G1 / sm8845→8G5 / sm7675→7P3 / mt6989→D9300 / mt6991→D9400）
2. **清理 legacy** `remove_legacy_modules`：卸载 CoreTurboScheduler / Oxygen / Kinetic / CTS / Kin_legacy 等旧模块
3. **部署守护** `deploy_cts_daemon`：复制 `Json/config_<SoC>.json` 为 `config.json` + 部署 `Kinetic` 二进制到 live 目录
4. **安装 APK** `install_cts_apk`：安装 `Kin.apk`（从兄弟目录 `CTS/cts` 寻找）
5. **热更新**：杀旧守护 + 覆盖到 live 目录
6. **vtools 部署** `init_vtools.sh`：部署 `powercfg.sh/json`，初始化 `mode.txt`（默认 `balance`）

#### 启动流程（[service.sh](magisk/service.sh)）

```
开机
  │
  ▼
post-fs-data.sh        # 早期: bind-mount 锁定 OPlus 节点
  │
  ▼
system.prop 生效       # 墓碑冻结/Vulkan/LMK/高刷
  │
  ▼
service.sh 启动
  │
  ├─ cleanup_legacy
  ├─ wait_boot_completed
  ├─ wait_until_login
  │
  └─ 单常驻循环（每 2s）:
       ├─ 拉起 Kinetic（如未运行）
       ├─ 校验 mode.txt 合法性
       ├─ cpuset 兜底
       └─ verify_status 写 module.prop description
```

### 7.2 守护进程运行时

#### 生命周期

```
main()
  ├─ 安装信号处理（SIGTERM/SIGINT/SIGPIPE）
  ├─ g_paid = License::readAuth().paid   # 原子读取
  ├─ MetricsServer.start()               # 抽象 socket listen
  ├─ Schedule schedule(g_paid)           # 构造，启动 9 线程
  └─ pause()                             # 主线程阻塞等信号
       │
       └─ 收到 SIGTERM → Schedule 析构 → 退出
```

#### 模式切换

用户在 App 点击模式 → App 写 `/sdcard/Android/Kin/mode.txt` → `fileWatchTask` 监听到变化 → `applyCurrentMode()` → 写 sysfs。

### 7.3 开发者视角：本地构建

#### 前置

- Android NDK r26d（环境变量 `ANDROID_NDK` 指向路径）
- Ninja
- CMake ≥ 3.18

#### 方式 A：build.sh

```bash
export ANDROID_NDK=/path/to/ndk
chmod +x build.sh
./build.sh arm64-v8a
# 产物：output/arm64-v8a/Kinetic
```

#### 方式 B：build_pack.py（一键构建+打包）

```bash
python3 build_pack.py
# 自动: 编 tweetnacl.c → 编 main.cpp → 打包
# versionCode 用 Unix 纪元秒保证递增
# 从兄弟目录 CTS/cts 寻找 Kin.apk
```

#### 构建开关（[CMakeLists.txt](CMakeLists.txt)）

| 开关 | 说明 |
| --- | --- |
| `NO_LICENSE` | `ON` 时定义 `KIN_NO_LICENSE` 宏，绕过卡密（公益版） |

#### 编译优化（Release）

`-Os -flto -fno-rtti -fvisibility=hidden` —— 体积优先，无 RTTI，符号隐藏。输出二进制名 `Kinetic`。

### 7.4 CI 构建（[.github/workflows/build.yml](.github/workflows/build.yml)）

双版本并行：

| 版本 | `NO_LICENSE` | 产物名 | 说明 |
| --- | --- | --- | --- |
| 捐赠版 | `OFF` | `Kin-juanzheng` | Ed25519 卡密授权 |
| 公益版 | `ON` | `Kin-gongyi` | 免授权，安装即用 |

- NDK r26d + Ninja
- 推送 / PR 只产内部 artifact
- `workflow_dispatch` 或打 tag 触发 bundle + GitHub Release
- 滚动发布（nightly）：手动触发，发布到公开 Releases

### 7.5 授权服务部署（外部私有组件）

授权服务（Express.js + libSQL/Turso）负责卡密生成/校验、设备绑定、支付回调与后台管理，
**不随本仓库分发**，部署方式、环境变量与数据库结构见其所属私有仓库。

本仓库只包含客户端侧逻辑：Ed25519 验签（`include/License.hpp` + `src/tweetnacl.c`）、
短时令牌校验与离线宽限。

### 7.6 卡密激活与令牌流转

```
用户 App 输入卡密
   │
   ▼
App → POST /api/verify {licenseKey, deviceId}
   │
   ▼
license-server:
   ├─ 校验卡密存在 + 未禁用 + 未绑定其他设备
   ├─ 绑定 (licenseKey, deviceId)
   ├─ signToken(msg=`licenseKey|deviceId|expiresAt|tokenNotAfter`, Ed25519私钥)
   └─ 返回 {token, expiresAt, plan}
   │
   ▼
App 本地 Ed25519 验签（用预置公钥）
   │ 验签通过
   ▼
App 写 /sdcard/Android/Kin/AI/auth.json {paid:true, plan, expiresAt}
   │
   ▼
守护 fileWatchTask 监听 → g_paid = true → 解锁完整功能
```

---

## 8. 关键数据流与状态机

### 8.1 AutoMode 数据流

```
powerRecorderTask (30s)
   │ 采样: 电流/温度/帧率
   ▼
autoModeTask (5min)
   │
   ├─ 1. 读取当前桶 (时段 × 工作日/周末)
   ├─ 2. 评估置信度 (obsX100)
   │     ├─ < ASB_CONF_LOW(350)  → 回退到 DAYPART_ONLY/CLASS/GLOBAL/SAFE
   │     └─ > ASB_CONF_HIGH(650) → 采信本桶 alpha
   ├─ 3. 应用覆盖层 (夜间/低电/热/anti-clamp)
   ├─ 4. AutoFsm.computeCaps(alpha) → Caps
   ├─ 5. writeFsmCaps(caps) → sysfs
   ├─ 6. ε-greedy 探索 (小概率换档)
   ├─ 7. 观察奖励 → 更新桶 alphaRaw/obsX100/dtMean/dtMad
   └─ 8. 持久化 auto_state.txt
```

### 8.2 GAMING 态 FAS 闭环

```
AutoFsm 进入 GAMING 态 (alpha=1.0, dwell≥25s)
   │
   ▼
AutoFas 启动
   ├─ 读帧源: Soc::kMeasuredFps (主) / dumpsys SF --latency (兜底≤1Hz)
   ├─ 计算误差: e = targetFps - measuredFps
   ├─ P 控制器: Δ = KP * e (KP=3e-4)
   ├─ 调整 maxB (大核上限)
   └─ 输出 Out{engaged:true, maxL, maxB}
   │
   ▼
Schedule.writeFsmCaps(Out) → sysfs
   │
   ▼
下一周期 (1s) 闭环
```

### 8.3 anti-clamp 对抗

```
cpuEnforceTask (1~2s)
   │
   ├─ 读 /sys/kernel/msm_performance/parameters/cpu_max_freq
   │
   ├─ 检测: 厂商是否压低了频率上限?
   │
   ├─ 是 → applyAntiClampNode() 反写期望值
   │       + AutoMode 覆盖层置位 anti-clamp
   │
   └─ 否 → 正常 enforce
```

### 8.4 事件驱动绑核

```
EventMonitor (proc connector, NETLINK)
   │
   ├─ PROC_EVENT_FORK  → child_tgid
   ├─ PROC_EVENT_EXEC  → child_tgid + comm
   │
   ▼
threadAllocTask
   ├─ 查 top-app 包名 (Utils::getTopAppFast)
   ├─ ThreadRules.resolveThread(pkg, thread) → cpu_set_t
   ├─ ThreadPriorityRules.resolve → (policy, prio)
   └─ sched_setaffinity / sched_setscheduler
```

---

## 9. 配置体系

### 9.1 配置层级（优先级从高到低）

```
1. /sdcard/Android/Kin/config.json   # App 下发, 热重载, 最高优先
2. /data/adb/modules/Kin/config.json # 模块内置 (从 Json/config_<SoC>.json 部署)
3. Config.hpp 编译期默认值            # 兜底
```

### 9.2 config.json 结构（由 [JsonConfig.hpp](include/JsonConfig.hpp) 解析）

```jsonc
{
  "Meta": { "version": "3.2", "soc": "8G3" },
  "Policy": { "defaultMode": "balance" },
  "Cpuset": {
    "topApp":    ["0-7"],
    "foreground":["0-5"],
    "background":["0-3"],
    "system":    ["0-3"]
  },
  "LaunchBoost": { "durationMs": 1500, "maxBig": 3302400 },
  "OfficialMode": {
    "powersave":    { "little": [300000, 1800000], "big": [300000, 2200000] },
    "balance":      { ... },
    "performance":  { ... },
    "fast":         { ... }
  },
  "SysTune":     { "battery": {...}, "balanced": {...}, "perf": {...} },
  "BootTrim":    { "targets": ["oplus.midas","olc","crashbox","logkit"] },
  "BgTrim":      { "intervalSec": 1800, "level": 80 },
  "TouchBoost":  { "durationMs": 500, "freq": 2800000 },
  "ThreadAlloc": { "rules": ["com.tencent.tmgp.sgame{UnityMain}=4-7", ...] },
  "Scheduler":   { "enforceIntervalMs": 1500 },
  "GpuFreq":     { "maxFreq": 710000000 },
  "AppProfile":  { "categories": {...}, "perapp": [...] },
  "AutoMode":    { "enabled": true, "exploreEps": 0.05 },
  "Performances": { ... }
}
```

### 9.3 每 SoC 配置文件

[Json/](Json) 目录下每个 `config_<SoC>.json` 是该 SoC 的出厂默认。`customize.sh` 安装时按识别结果复制为 `config.json`。

| 文件 | SoC | 设备示例 |
| --- | --- | --- |
| `config_8E.json` | 骁龙 8 Elite (sm8750) | OnePlus 13 |
| `config_8E5.json` | 骁龙 8 Elite Gen 5 (sm8850) | OnePlus 14 |
| `config_8G1.json` | 骁龙 8 Gen 1 (sm8450) | OnePlus 11 |
| `config_8G2.json` | 骁龙 8 Gen 2 (sm8550) | OnePlus 12 |
| `config_8G3.json` | 骁龙 8 Gen 3 (sm8650) | OnePlus 13 (early) |
| `config_8G5.json` | 骁龙 8 Gen 5 (sm8845) | — |
| `config_7P3.json` | 骁龙 7+ Gen 3 (sm7675) | — |
| `config_D9300.json` | 天玑 9300 (mt6989) | — |
| `config_D9400.json` | 天玑 9400 (mt6991) | — |

### 9.4 perapp_powermode.txt

[Json/perapp_powermode.txt](Json/perapp_powermode.txt) —— 预填应用画像表，包名通配符 → CatPolicy / 覆盖模式。`AppProfileModel::matchPackage` 用迭代式通配符匹配。

### 9.5 system.prop（[magisk/system.prop](magisk/system.prop)）

| 类别 | 关键属性 | 作用 |
| --- | --- | --- |
| 墓碑冻结 | `persist.sys.oppo.tombstone.enable=1` + `freeze_policy=3` | 激进冻结后台 |
| VSync | `debug.adreno.disable_vsync=1`, `debug.sf.triple_buffer=0` | 关三缓冲降延迟 |
| 日志关闭 | `persist.sys.log.user=0` 等 | 减少 logcat I/O |
| 热限关闭 | `oplus.dex.tempcontrol=false` | 解除 OPlus 热限 |
| Vulkan | `debug.hwui.vulkan_safe_mode=1` | Vulkan 兼容 |
| LMK | `persist.sys.oplus.lmkd_super_critical_threshold_*=256/512` | 提高 LMK 阈值 |
| 高刷解禁 | `debug.graphics.video_default_frame_rate.disabled=true` | 放开视频/游戏默认帧率封顶 |

> 注：`ro.*` 属性运行时不可修改，文件中已注释说明。

---

## 10. 构建与发布

### 10.1 CMakeLists.txt 要点（[CMakeLists.txt](CMakeLists.txt)）

- 项目名 `Kin`，VERSION `3.2`
- 语言：`CXX`（C++23）+ `C`（C99）
- 关键开关：`NO_LICENSE`（→ `KIN_NO_LICENSE` 宏）
- Release：`-Os -flto -fno-rtti -fvisibility=hidden`
- 输出二进制名：`Kinetic`
- `tweetnacl.c` 作为独立 C 编译单元（不参与 C++ RTTI）

### 10.2 build.sh（[build.sh](build.sh)）

- bash 脚本，支持 `arm64-v8a`
- 依赖 `ANDROID_NDK` 环境变量
- 使用 Ninja

### 10.3 build_pack.py（[build_pack.py](build_pack.py)）

- Python 一键构建+打包
- `versionCode` 用 Unix 纪元秒保证递增
- 先编 `tweetnacl.c` 为 `.o`，再编 `main.cpp`
- 从兄弟目录 `CTS/cts` 寻找 `Kin.apk`

### 10.4 CI 双版本（[.github/workflows/build.yml](.github/workflows/build.yml)）

| 阶段 | 触发 | 产物 |
| --- | --- | --- |
| build | push/PR/dispatch | `Kin-juanzheng` + `Kin-gongyi`（artifact, 1 天） |
| bundle | dispatch/tag | 整包（二进制 + Json + README + LICENSE, 3 天） |
| rolling-release | dispatch | nightly zip + 裸二进制 + SHA256（公开 Releases, prerelease） |
| tag release | tag `v*` | `Kin-<tag>.zip` + sha256（GitHub Release） |

### 10.5 模块更新

- [update.json](update.json)：`version` / `versionCode` / `zipUrl` / `changelog`，供 Magisk 内置更新检查
- 当前 `version=Ⅴ⁰⁵`，`versionCode=260615`

### 10.6 eBPF 后端构建（可选）

需额外：`clang-bpf` / `bpftool` / `libbpf` + 内核 BTF 支持。该后端未随本仓库分发，需自行实现。

---

## 11. 附录：术语表

| 术语 | 全称 / 含义 |
| --- | --- |
| **ASB** | AutoSystemBoost，本项目的 AutoMode/FSM/FAS 移植来源 |
| **FSM** | Finite State Machine，6 状态有限状态机 |
| **FAS** | Frame Aware Scheduling，帧感知调度 |
| **anti-clamp** | 反厂商压频，主动对抗 msm_performance 压频 |
| **proc connector** | Linux NETLINK_CONNECTOR + CN_IDX_PROC，进程事件通知 |
| **SCM_RIGHTS** | Unix domain socket 传递 fd 的机制，用于 memfd 零拷贝 |
| **memfd_create** | 创建匿名内存文件的系统调用，用于共享内存 |
| **CatPolicy** | 9 类应用画像策略（game/audio/video/image/social/news/maps/productivity/undefined） |
| **ε-greedy** | 以 ε 概率探索新动作，1-ε 概率取当前最优的探索策略 |
| **MAD** | Mean Absolute Deviation，平均绝对偏差，用于负载稳定性度量 |
| **LMK** | Low Memory Killer，Android 低内存杀进程机制 |
| **PSI** | Pressure Stall Information，内核压力指标（memory pressure 用于 AB 测试） |
| **Ed25519** | 椭圆曲线签名算法，用于卡密令牌签发 |
| **tweetnacl** | 公有领域 NaCl 加密库的极简实现 |
| **libSQL/Turso** | SQLite 的开源 fork，支持本地 + 云端 |
| **SoC** | System on Chip，如骁龙 8 Gen 3 (sm8650) |
| **governor** | Linux cpufreq 调频策略（walt/schedutil/hmbird） |
| **cpuset** | cgroup cpuset 控制器，控制进程可用的 CPU 核 |
| **CFS** | Completely Fair Scheduler，Linux 公平调度器 |
| **RT** | Real-Time，SCHED_FIFO / SCHED_rr 实时调度策略 |
| **NO_LICENSE** | 构建开关，公益版绕过卡密 |
| **捐赠版 / 公益版** | 双版本发布策略：捐赠版需卡密，公益版免授权 |

---

> **文档版本**：基于仓库当前快照生成。如代码有更新，请重新分析以保持一致。
> **维护建议**：当新增模块/类时，在 §4 模块职责表与 §5 关键类说明中同步补充；当修改构建流程时，同步 §7 与 §10。
