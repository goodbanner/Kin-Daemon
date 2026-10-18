// 前台探测(Activity dumpsys)的节流档位。
//
// 背景: 多窗口游戏扫描每拍都要问一次 ActivityManager「当前有哪些可见 Task」, 而这条 dumpsys
// 只在小窗/分屏里游戏不是顶层包时才用得上 —— 绝大多数会话里它都是空转。原实现固定 2s 一次,
// 也就是亮屏期间常驻 0.5 次/秒的 popen+fork。
//
// 上游 ASB V64 用同一思路把前台探测从「最多 3 次 fork/tick」降到按活动度分级的缓存 TTL
// (8s / 25s / 120s)。这里取同样的三档, 但判据用 Kin 已有的遥测, 不引入新采样:
//   · 正在重载/游戏态, 或 GPU/CPU 已经起来 → 8s   (刚进小窗游戏的最可能窗口)
//   · 中等活动                              → 25s
//   · 安静(什么都低)                        → 120s
//
// 安全性: 小窗里真在跑的游戏会把 GPU/CPU 顶起来, 下一拍(2s)就落到 8s 档, 所以检测延迟
// 不会退化成 120s —— 只有"确实什么都不动"时才会降到 120s, 而那时也没有游戏可找。
#pragma once

namespace kin {

struct ProbeCadence {
    int activeMs        = 8000;
    int mediumMs        = 25000;
    int quietMs         = 120000;
    int gpuActivePct    = 50;
    int cpuActivePct    = 50;
    int gpuMediumPct    = 25;
    int cpuMediumPct    = 30;
};

// 多窗口扫描的缓存 TTL(毫秒)。gpuPct/cpuBusy 传 -1 表示读不到 → 按安静档(省电优先)。
inline int multiWindowScanTtlMs(int gpuPct, int cpuBusy, bool heavyNow,
                                const ProbeCadence& c = ProbeCadence{}) {
    if (heavyNow) return c.activeMs;
    if (gpuPct >= c.gpuActivePct || cpuBusy >= c.cpuActivePct) return c.activeMs;
    if (gpuPct >= c.gpuMediumPct || cpuBusy >= c.cpuMediumPct) return c.mediumMs;
    return c.quietMs;
}

} // namespace kin
