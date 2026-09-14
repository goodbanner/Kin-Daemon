# Kin-Daemon 优化交接

## 范围与状态

未最终完成。工作仓库为 `E:\项目文件\Kin\Kin-Daemon`；保留现有未提交改动，不用 reset/checkout 清理。不要将 Windows 测试通过等同于 Android 实机验证。

## 最新一轮：节点写入验证原语

- 新增并测试 `NodeWriter.hpp`：区分 Written/Unchanged/Invalid/OpenFailed/ReadFailed/TooLong/WriteFailed/Mismatch，支持 existing-only 写入、可选写后回读；不自动接入 write-only 节点，避免改变 GPU/触发型节点语义。
- 新增 `test_node_writer.cpp`，覆盖缺失、同值跳过、变化写入和回读；主机 CTest **14/14 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- NodeWriter 当前是验证原语，生产路径仍由 Utils/Function 逐步迁移；下一步应按节点能力把 CPU/cpuset/uclamp 纳入并记录每节点结果，不能一次性替换所有 FileWrite。

## 前轮：策略条目校验

- 新增 `PolicyEntry`/`validatePolicyEntry`，统一校验 App policy 与 FSM policy 的 maxL/maxB/gpu 范围；非法值分别回退到不限制/未设置，cores 只接受明确 `0`，并强制 cpu0 在线。
- JsonConfig 的两类策略表实际调用同一校验器；测试覆盖非法百分比、GPU、cores 和 FSM/App 不同回退语义。
- 主机 CTest **12/12 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 仍缺各条目间的 min/max 依赖、GPU 与 CPU 联合约束、校验失败原因诊断；真实策略 resolver 的每簇输出与 App policy cores 接管仍待实机验证。

## 前轮：完整基础参数校验

- ConfigValidation 扩展到夜间小时、tick 周期、学习率、掉电阈值和 warm/cool/hot/vhot/veto 温度关系。
- JsonConfig 统一回写这些参数；测试覆盖越界回退、依赖关系和合法值保持。
- 主机 CTest **12/12 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 仍未覆盖 app_policy/fsm_policy 每个条目的语义依赖、conf/MAD/obs 参数、quiet-night 边界和配置错误原因诊断；不等于全部配置已完成校验。

## 前轮：功率阈值配置校验

- 配置校验扩展到 float 功率阈值与依赖：普通 throttle 必须 >0，critical 必须严格高于普通阈值且不超过 30W，boost jank load 限制在 0..32。
- JsonConfig 解析后统一回写校验结果；测试覆盖非法 float、临界阈值自动回退和合法值保持。
- 主机 CTest **12/12 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 尚未覆盖夜间/tick/学习系数、FAS/FSM policy 每项依赖、校验失败原因诊断和实机功率标定。

## 前轮：扩展配置统一校验

- `ConfigValidation.hpp` 扩展覆盖 camera_hold 百分比、game uclamp、power boost 值/PSI/busy/cap、high/critical throttle 百分比、FAS 目标帧率。
- `JsonConfig` 解析后统一调用同一校验器并回写这些字段，避免各段独立校验产生不一致。
- 测试覆盖越界回退、依赖关系和合法值保持；主机 CTest **12/12 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 尚未纳入 float 功率阈值、夜间/tick/学习参数、FSM policy/app policy 每项依赖和校验失败原因诊断；不能宣称配置校验全部完成。

## 前轮：配置统一校验

- 新增 `ConfigValidation.hpp`，统一校验低电阈值依赖、FSM 热进出依赖、anti-clamp soft/hard 依赖、attack_temp 和 explore/reward 范围。
- `JsonConfig::parseAutoMode` 实际调用校验器，合法配置行为不变，非法配置回退安全默认/相关默认。
- 测试先失败（非法 thermal enter 的 exit 期望未随回退同步），修正后通过；主机 CTest **12/12 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 尚未覆盖所有 AutoMode 参数（FAS/FSM policy/boost/camera 等），也未将校验失败原因写入状态诊断；本轮不是完整配置审计。

## 前轮：策略输入边界

- 审计发现 `PolicyResolver::appPolicy` 可能接受负 max/异常 min；新增归一化：无效 max 回退本 tick FSM base，负 min 回退 base，并重新保证 min<=max。
- 测试先失败 exit=14，修复后通过；主机 CTest **11/11 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 尚缺统一 Config 校验（thermal/FSM/FAS/anti-clamp 参数依赖）、真实低电 cap 在最终频率合并中的安全覆盖；本轮只修 resolver 入参。

## 前轮：/proc 存活扫描节流

- 焦点兜底使用的 `lastGoodFgPkg_` 存活检查增加 2s 缓存，失败路径不再每个 FSM tick 扫整棵 `/proc`。
- 新增通用 `PresenceCache.hpp` 和 TTL/key 切换测试；当前粘滞游戏路径已有局部 2s 节流，后续可迁移到 helper，配置重载的低频检查仍保留。
- 主机 CTest **10/10 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- `findGamePid` 仍只在 FAS 进场/换游戏调用；完整真实 `/proc` 计数和实机唤醒功耗未测。

## 前轮：前台来源诊断

- AutoMode 状态追加 `foreground_source`；scheduler 在实际返回路径标记 accessibility/accessibility_blacklist/top_app_cpuset/window_focus/activity_task/window_fallback 等来源。
- 不改变前台选择顺序；仅补充诊断。推送黑名单显式标为 accessibility_blacklist，避免 App 误以为没有来源。
- 主机 CTest **9/9 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 仍需测试 getForegroundPkg 端到端来源、粘滞包与真实前台分离；write result 仍非每节点聚合。

## 前轮：GPU 读后写去重

- `Function::writeSysfs` 对 GPU 频率节点先尝试精确读当前值；明确相等才跳过，不可读/读失败仍写，兼容 write-only sysfs。
- 比较复用 `NodeValue.hpp`，不会再因固定前缀截断误判；写入值仍由原有频率 snap/范围校验决定。
- 主机 CTest **9/9 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- GPU 写入结果尚未纳入 WriteStatus batch；GPU 实际路径/节点能力需实机或 Linux sysfs 测试验证。

## 前轮：批量写入结果

- `WriteStatusTracker` 增加 batch 聚合；`writeFsmCaps()` 在 CPU 簇写入前后开启/结束 batch，状态文件读取 `writeBatchStatus()`，不再把最后一次单节点结果冒充整批结果。
- 聚合规则：failed 优先，其次 written，全部 unchanged 才 unchanged；未知/无节点不会被当成功。仍只覆盖 FileWriteIfChangedBlocking 的节点，触控/初始化/直接 FileWriteBlocking 路径不自动纳入。
- 主机 CTest **9/9 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 已知限制：批次只覆盖 FSM scaling writes；Fengchi、GPU 原子通道、uclamp/boost 同拍结果尚未统一纳入；写成功仍不是内核接受确认。

## 前轮：写入结果诊断

- 新增 `WriteStatus.hpp`：`unknown/written/unchanged/failed`；`Utils::FileWriteIfChangedBlocking` 实际记录比较和写入结果。
- AutoMode 状态追加 `write_verification`，scheduler 在 CPU caps 写入后同步；此字段只说明 daemon 写路径结果，不说明内核最终接受。
- 主机 CTest **9/9 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- tracker 当前是 Utils 实例的最后一次变化检测结果；直接 `FileWriteBlocking` 路径未自动纳入，尚缺每节点/每簇聚合与写后回读。

## 前轮：热状态诊断

- 新增 `ThermalStatus.hpp` 纯诊断分类：hard_clamp/critical/hot/warm_skin/vendor_limited/normal/unknown；只读取已有决策输入，不改变 AutoFsm 行为。
- AutoMode 状态文件追加 `thermal_state`，与既有 battery_state、policy_reason 并列；协议明确为诊断而非硬件确认。
- 测试覆盖 hard clamp 优先、热/皮肤/厂商限制/未知；主机 CTest **9/9 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 仍需实现 `foreground_source`、`last_write_result`、真实写入失败归类及恢复；GPU/fengchi/blacklist 仍不是统一 resolver 的完整来源。

## 前轮：真实低电迟滞

- 抽取 `BatteryPolicy.hpp`，AutoMode::alphaFor 实际调用该类维护迟滞/800-900 alpha 地板，不再仅测试模拟 cap。
- 修复 charging=true 但 capacity=-1 时旧逻辑提前返回、低电迟滞不退出；拒绝 >100 异常容量解除低电锁。保留正常 low_batt 进入 / 40% 退出行为；开关关闭只停覆盖，迟滞仍更新。
- 状态追加 battery_state（disabled/charging/normal/low/critical/unknown/low_held_unknown）。测试覆盖阈值、迟滞、充电未知容量、异常容量、开关、状态 token；旧逻辑先失败 exit=4，修正后通过。
- 主机 CTest **8/8 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。
- 未实现：低电 alpha 对应上限在用户 FSM 百分比覆盖/boost/风驰之后的硬性优先级。不要把本轮状态机测试说成完整硬件安全覆盖验证。

## 前轮：CPU 策略诊断

- `PolicyStatus.hpp` 通过 AutoMode::writeFsmStatus 实际追加 resolved_policy/policy_reason/policy_scope=cpu/fas_state/write_verification=unverified，保留旧字段。风驰分支显示 rom_managed/fengchi_takeover，而非错误标记普通 FSM cap。
- 对输出 token 作长度/字符验证；未验证硬件写入，不输出成功断言。
- resolver 对候选已含 cap（上限相等）也保留来源，回归先失败 exit=13、修复后通过。候选 reason 不再仅根据 fgCat 非空就标成 profile。
- policy_status 测试覆盖普通/风驰/FAS 状态及换行注入；主机 CTest **7/7 通过**，Android daemon 编译链接成功（8 个现有 shadow 警告），diff 检查通过。
- 仍需区分真实前台与粘滞游戏包、输出 foreground_source；旧 cpu_max 单位文档与实际调用 /1000 不一致，风驰分支旧 caps 值不代表实际硬件目标。这些未在本轮静默改变协议。
- thermal_state 后续已接入；last_write_result 仍未实现；状态文件端到端与实机验证未完成。

## 前轮：普通 CPU cap 来源接入

- resolver 拆分 thermal/battery/power/app/camera/fas/other 槽，避免 App 和 FAS 限制不同簇时覆盖彼此的原因。诊断并列优先级按上述顺序，数值始终取最小。并非完整 ROM/黑名单策略优先级。
- scheduler 登记 FAS、类别、相机实际收紧值和高/临界功率 cap。末端候选可 boost，但不能再放开已登记限制。这是行为修复，需要实机检查帧率/响应性。
- 新增跨来源顺序/归因回归测试：旧共享槽失败 exit=11，修正后通过。主机 CTest 6/6、Android daemon 链接、diff 检查均通过；8 个既有 shadow 警告。
- 后续已修复 cap 与候选相等时的归因，并接入状态字段；多个同类型提交仍共用一个元数据槽，尚不是完整的每簇原因解释。
- 尚未将真实低电状态机转换为显式 cap，风驰/黑名单/GPU 仍走既有分支。不要宣称全部策略优先级已完成。

## 前轮：Activity Task 探测节流

- `ForegroundCache.hpp` 从 Utils 提取实际 Activity 快照缓存。每实例只允许一个探测，其他并发调用立即返回 invalid，不排队等待慢 dumpsys；失败也缓存，2s 后才重试；TTL 从探测完成计时。
- fresh/ttl=0 不再绕过 Activity 2s 下限；无障碍推送不经此缓存，响应不受影响。无推送时事件响应可能增加最多约 2s 延迟。
- `test_foreground_cache.cpp` 可控时钟验证失败节流/恢复、成功 TTL、慢探测计时，线程 gate 验证同实例并发只执行一个 probe。旧算法先失败 exit=1，修复后通过。
- 本轮主机 CMake/CTest **6/6 通过**，Android daemon 编译链接成功（8 个既有 shadow 警告），diff 检查通过。测试没有执行实际 dumpsys，未测实机功耗。
- 仍未限频：独立窗口焦点回退、getTopAppCached 的并发空结果路径、部分 /proc 扫描；多窗口发现逻辑未改。

## 前轮：节点值比较

- 新增 `include/NodeValue.hpp`，被 `Utils::FileWriteIfChangedBlocking` 实际调用。逐段读取到 EOF，处理 EINTR；最大 4096 字节，超过限制返回 TooLong，不能把相同前缀误认成相等。仅修剪行尾空白。
- 新增真实临时文件测试 `tests/test_node_value.cpp`，覆盖不存在、空值、外部漂移、行尾/行首空白、255 字节前缀、4096/4097 边界。保留旧算法时测试失败 exit=3；修复后通过。
- 同步 ASCII 副本后 CMake/CTest **5/5 通过**；原仓库 Android NDK daemon 编译链接通过，8 个既有 shadow 警告；diff 检查通过。
- 这只验证读取比较，不等于完整 sysfs 写入/恢复、并发所有权或 Android 实机验证。读取失败仍返回 false，写后验证与失败诊断尚未实现。

## 前轮实际改动

- PolicyResolver 不再把初始 FSM 上限初始化为安全 cap，避免无安全限制时误挡功率 boost。
- cap 与普通候选分开保存；普通候选更新不清除已登记限制。
- 最终诊断选择实际限制某簇的 cap，跨类型并列优先 thermal、battery、普通 cap。同一普通 cap 槽仍只保留聚合上限/最后收紧的元数据，尚不是完整策略优先级模型。
- scheduler 的 joint-DVFS 热限制登记到 resolver，防后续功率 boost 撤销此限制；风驰旁路未改动。
- 新增 boost 误限复现、安全原因保持、调用顺序、非法 cap、逐 tick 重建测试。boost 复现先失败 exit=6，修正后通过。

## 验证证据与工具

- `D:\kin-daemon-verify\build`：同步 include/tests/CMakeLists.txt 后 CMake 构建成功，CTest 4/4 通过。
- 原仓库 `build-android-arm64`：Android NDK 编译链接成功，产物 `bin\Kinetic`；8 个现有 shadow 警告。未安装到设备、未运行。
- `git diff --check` 通过。
- Windows CTest 不注册 Linux 专用 syswrite 测试；该测试未运行。
- 实际工具已存在，不再重复安装或声称缺失：
  - `C:\Program Files\CMake\bin\cmake.exe` / `ctest.exe`
  - `D:\msys64\mingw64\bin\g++.exe`（执行前把该 bin 加入 PATH，保留原 PATH）
  - Android 构建缓存已有 NDK `C:\Android\sdk\ndk\28.2.13676358` 和 Ninja `C:\Android\sdk\cmake\3.31.6\bin\ninja.exe`。
- Android 可直接在原中文路径构建。Windows 主机 CMake 使用 ASCII 副本；每轮先同步本轮修改，不用旧副本冒充新验证。
- Git Bash 下不要使用 `cmd /c`（参数转换会导致命令未执行）；可用绝对路径工具或单引号包裹 PowerShell 命令。PATH 用双引号保留 `$PATH` 展开。

## 下一步必须处理

1. 完整策略合并仍未完成：普通 CPU 的 FAS/App 类别/相机/功率/joint-DVFS 热 cap 已登记；未接管风驰、黑名单、真实电量状态机或 GPU 所有优先级。低电测试仅测试合并器数值，不覆盖真实电量状态机。
2. 之前对 syswrite 的结论过强：FreqWriter 仍会命中内存缓存提前 return，未每次核对节点。读后写也不等于跨模块写入所有权管理。
3. FileWriteIfChangedBlocking 的截断前缀误判已修复，read EINTR 已处理；写后回读、失败分类传播、写入成功与同值跳过、恢复失败仍需测试。不要机械地把写-only/触发型节点改成 read-before-write。
4. 退出恢复路径可能在恢复失败时清空 held 标记，需故障注入验证。uclamp 数值格式/百分比与 sysctl 0..1024 单位需分别核对。
5. Activity 缓存失败节流及同实例并发重复探测已修复；多窗口周期 dumpsys、焦点兜底及 /proc 扫描仍待处理，不能宣称整个高频进程开销已解决。
6. Linux syswrite 测试、真实调度器集成测试、退出恢复/黑名单/风驰/FAS 互斥测试、统一状态字段、实机功耗/调度验证仍未完成。

每轮应完成可验证行为，不再仅替换几个调用后把静态 grep 当测试通过。任务全部验收后再暂停对应周期任务，未验收不得标记最终完成。
