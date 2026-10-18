// 息屏背景 uclamp 上限决策的单测。
// 重点是"只压不抬"和"读不到就不动"——这两条一旦破, 要么每拍重写节点, 要么把后台算力凭空抬高。
#include "IdleUclamp.hpp"
#include <cstdio>

namespace {
bool eq(int got, int want, const char* what) {
    if (got == want) return true;
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    return false;
}
constexpr int kCap = 18;
}  // namespace

int main() {
    // 典型场景: 息屏 + 平台值 29% → 压到 18。
    if (!eq(kin::deepIdleBgUclampTarget(29, true, kCap), 18, "29 -> 18")) return 1;
    if (!eq(kin::deepIdleBgUclampTarget(100, true, kCap), 18, "100 -> 18")) return 2;

    // 只压不抬: 已经更低就一律不动(否则会每拍重写, 还可能抬高平台值)。
    if (!eq(kin::deepIdleBgUclampTarget(18, true, kCap), -1, "already at cap")) return 3;
    if (!eq(kin::deepIdleBgUclampTarget(5, true, kCap), -1, "already lower")) return 4;
    if (!eq(kin::deepIdleBgUclampTarget(0, true, kCap), -1, "zero")) return 5;

    // 不在 DEEP_IDLE 一律不动(还原由 OwnedNode 负责, 不在这里猜)。
    if (!eq(kin::deepIdleBgUclampTarget(29, false, kCap), -1, "screen on")) return 6;

    // 读不到当前值就不猜。
    if (!eq(kin::deepIdleBgUclampTarget(-1, true, kCap), -1, "unreadable")) return 7;

    // 开关语义: cap<=0 或 cap>=100 等于关闭(>=100 时"压到 100"没有意义)。
    if (!eq(kin::deepIdleBgUclampTarget(29, true, 0), -1, "cap 0 disabled")) return 8;
    if (!eq(kin::deepIdleBgUclampTarget(29, true, -5), -1, "cap negative")) return 9;
    if (!eq(kin::deepIdleBgUclampTarget(29, true, 100), -1, "cap 100 disabled")) return 10;

    // 目标值永远不高于当前值(绝不抬高)且不高于 cap。
    for (int cur = 0; cur <= 100; ++cur) {
        for (int cap = 1; cap <= 99; ++cap) {
            const int t = kin::deepIdleBgUclampTarget(cur, true, cap);
            if (t < 0) continue;
            if (t > cur || t > cap || t != cap) {
                std::printf("FAIL target cur=%d cap=%d -> %d\n", cur, cap, t);
                return 11;
            }
        }
    }

    std::puts("PASS idle uclamp: cap-only-down, unreadable skip, disabled semantics");
    return 0;
}
