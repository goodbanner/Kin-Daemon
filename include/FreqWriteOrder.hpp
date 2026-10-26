#pragma once

namespace kin {

// 同时改 scaling_min/max 时, 新下限高于当前上限则必须先写上限。
// 否则内核返回 EINVAL, 下限停在旧值(例如智能档把大核压到 800MHz 后切到风驰,
// 1200MHz 的下限写不进去)。
// 新上限低于当前下限时维持先写下限: 先把地板降下来, 上限才写得进去。
// curMin/curMax 为 0 表示读不到当前节点, 保持历史上的「先 min 后 max」。
// 返回 true 表示本拍要先写 max。
inline bool writeMaxBeforeMin(int curMin, int curMax, int newMin, int newMax) {
    if (curMax > 0 && newMin > curMax) return true;
    if (curMin > 0 && newMax < curMin) return false;
    return false;
}

}  // namespace kin
