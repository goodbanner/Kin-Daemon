// 息屏(DEEP_IDLE)背景 uclamp 上限决策(移植自 ASB V64 asb_fsm.h)。
//
// 上游现场数据: DEEP_IDLE 是每次抓取里第二大的阶段(214 个样本里 73 个, 140 mA)。GPU 已经压住、
// CPU 频率也低, 但 background 组还带着 profile 的 uclamp.max(该机型 29%)—— 一个被唤醒的同步任务
// 仍然可以向调度器要近三分之一峰值算力, 而屏幕上没有任何人在等它。上游实测 140 mA → 50 mA。
//
// 18 而不是 0: 空的 uclamp 等于告诉调度器"这个任务完全不需要 CPU", 工作会直接停住
// (上游在 profile 轨道上踩过这个坑)。18% 留了一个能干活的地板, top-app / foreground 不动,
// 所以解锁速度和以前一样。
#pragma once

namespace kin {

// 返回应写入的值; -1 = 不改动。
//   · 不在 DEEP_IDLE、开关关闭、cap 无效(<=0 或 >=100) → 不动
//   · 读不到当前值(-1) → 不动(不猜)
//   · 当前已经 <= cap → 不动(只压不抬, 也避免每拍重写)
inline int deepIdleBgUclampTarget(int currentPct, bool deepIdle, int capPct) {
    if (!deepIdle) return -1;
    if (capPct <= 0 || capPct >= 100) return -1;
    if (currentPct < 0) return -1;
    if (currentPct <= capPct) return -1;
    return capPct;
}

} // namespace kin
