// 功率 boost 节点选择的单测。
// 修的是一个真实缺陷: 原实现用 sched_util_clamp_min 的存在去选中"sched_boost"模式,
// 探测节点 ≠ 写入节点 —— 在只有前者没后者的内核上会每拍重试一次写不存在的节点。
#include "PowerBoostNode.hpp"
#include <cstdio>

namespace {
bool eq(int got, int want, const char* what) {
    if (got == want) return true;
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    return false;
}
}  // namespace

int main() {
    // 两种节点都在: 优先 cgroup uclamp.min(能真正表达百分比 boost)。
    if (!eq(kin::selectPowerBoostMode(true, true), 1, "both present prefers cgroup")) return 1;

    // 只有 cgroup: 用它。
    if (!eq(kin::selectPowerBoostMode(true, false), 1, "cgroup only")) return 2;

    // 只有 sched_boost: 用它。
    if (!eq(kin::selectPowerBoostMode(false, true), 2, "sched_boost only")) return 3;

    // 都没有: 整段跳过 —— 绝不能"探测到别的节点"就假定这个节点存在。
    if (!eq(kin::selectPowerBoostMode(false, false), 0, "neither present")) return 4;

    // 回归护栏: 返回值只能是 0/1/2, 不允许出现旧的"模式 3"(它的写入目标与模式 2 相同,
    // 只会让日志与状态更难解释)。
    for (int cg = 0; cg < 2; ++cg)
        for (int boost = 0; boost < 2; ++boost) {
            const int m = kin::selectPowerBoostMode(cg != 0, boost != 0);
            if (m < 0 || m > 2) {
                std::printf("FAIL mode out of range cg=%d boost=%d -> %d\n", cg, boost, m);
                return 5;
            }
        }

    std::puts("PASS power boost node: selection follows the node actually written");
    return 0;
}
