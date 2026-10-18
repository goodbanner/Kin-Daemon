// 多窗口扫描 dumpsys 节流档位的单测。
// 这是"省电"改动: 断言的核心是安静时真的不扫(120s), 而重载/游戏态仍然敏感(8s)。
#include "ProbeCadence.hpp"
#include <cstdio>

namespace {
bool eq(int got, int want, const char* what) {
    if (got == want) return true;
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    return false;
}
const kin::ProbeCadence k{};
// 期望的绝对档位(上游 V64 的 8s/25s/120s)。这里写死字面值而不是引用 k.*,
// 否则把默认值改回 2000 也能“自洽通过”—— 测试就白写了。
constexpr int kActive = 8000;
constexpr int kMedium = 25000;
constexpr int kQuiet  = 120000;
}  // namespace

int main() {
    // 默认档位必须是上游那三档, 且安静档远大于旧实现的固定 2s。
    if (!eq(k.activeMs, kActive, "default active")) return 100;
    if (!eq(k.mediumMs, kMedium, "default medium")) return 101;
    if (!eq(k.quietMs, kQuiet, "default quiet")) return 102;
    if (!(kQuiet > 2000)) { std::printf("FAIL quiet tier not above legacy 2s\n"); return 103; }

    // 安静: 什么都低 → 2 分钟一次(原来固定 2s)。
    if (!eq(kin::multiWindowScanTtlMs(5, 10, false), kQuiet, "idle")) return 1;
    if (!eq(kin::multiWindowScanTtlMs(0, 0, false), kQuiet, "zero load")) return 2;
    // 读不到遥测(-1)按安静档处理: 省电优先, 不能因为读不到就狂扫。
    if (!eq(kin::multiWindowScanTtlMs(-1, -1, false), kQuiet, "unknown telemetry")) return 3;

    // 中等活动。
    if (!eq(kin::multiWindowScanTtlMs(25, 10, false), kMedium, "gpu medium edge")) return 4;
    if (!eq(kin::multiWindowScanTtlMs(10, 30, false), kMedium, "cpu medium edge")) return 5;
    if (!eq(kin::multiWindowScanTtlMs(40, 40, false), kMedium, "both medium")) return 6;

    // 重载/游戏态: 仍然敏感。
    if (!eq(kin::multiWindowScanTtlMs(50, 10, false), kActive, "gpu active edge")) return 7;
    if (!eq(kin::multiWindowScanTtlMs(10, 50, false), kActive, "cpu active edge")) return 8;
    if (!eq(kin::multiWindowScanTtlMs(0, 0, true), kActive, "heavy state wins")) return 9;
    if (!eq(kin::multiWindowScanTtlMs(-1, -1, true), kActive, "heavy with no telemetry")) return 10;

    // 边界: 差一格就必须降档, 不能把"刚好不到"也算成活动。
    if (!eq(kin::multiWindowScanTtlMs(24, 0, false), kQuiet, "gpu just under medium")) return 11;
    if (!eq(kin::multiWindowScanTtlMs(0, 29, false), kQuiet, "cpu just under medium")) return 12;
    if (!eq(kin::multiWindowScanTtlMs(49, 0, false), kMedium, "gpu just under active")) return 13;
    if (!eq(kin::multiWindowScanTtlMs(0, 49, false), kMedium, "cpu just under active")) return 14;

    // 单调性: 越安静档位越长, 否则分级就没意义。
    if (!(k.quietMs > k.mediumMs && k.mediumMs > k.activeMs)) {
        std::printf("FAIL cadence not monotonic\n"); return 15;
    }

    // 任意输入下都必须落在三个档位之一, 且永不低于 2s(缓存自身的重试下限)。
    for (int gpu = -1; gpu <= 100; gpu += 1) {
        for (int cpu = -1; cpu <= 100; cpu += 1) {
            for (int heavy = 0; heavy < 2; ++heavy) {
                const int v = kin::multiWindowScanTtlMs(gpu, cpu, heavy != 0);
                if (v != kActive && v != kMedium && v != kQuiet) {
                    std::printf("FAIL ttl out of range gpu=%d cpu=%d heavy=%d -> %d\n",
                                gpu, cpu, heavy, v);
                    return 16;
                }
                if (v < 2000) { std::printf("FAIL ttl below retry floor\n"); return 17; }
            }
        }
    }

    std::puts("PASS probe cadence: idle/medium/active tiers, boundaries, monotonic");
    return 0;
}
