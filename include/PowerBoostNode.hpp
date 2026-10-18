// 功率 boost 的写入节点选择(纯函数, 便于单测)。
//
// 修的是一个真实缺陷: 探测条件与写入节点不一致。
// 原实现按「sched_util_clamp_min 是否存在」选中模式 2, 但模式 2 的写入目标是 sched_boost。
// 在 sched_util_clamp_min 存在而 sched_boost 不存在的内核上, 结果就是:
//   · 选中一个写不进去的模式, boost 静默失效;
//   · 因为写入失败时状态不落定, 每个 tick 都会重试一次失败写入(不存在的节点上的写风暴)。
//
// 规则: 选择必须以【实际会被写入的节点】是否存在为准。
// sched_util_clamp_min 只是 UCLAMP_MIN 的"允许上限"(0..1024), 不是 boost 源, 不能拿它当选型依据。
#pragma once

namespace kin {

// 1 = cgroup cpu.uclamp.min(百分比 0..100)
// 2 = /proc/sys/kernel/sched_boost(0/1)
// 0 = 无可用接口, 整段跳过
inline int selectPowerBoostMode(bool cgroupUclampExists, bool schedBoostExists) {
    if (cgroupUclampExists) return 1;
    if (schedBoostExists) return 2;
    return 0;
}

} // namespace kin
