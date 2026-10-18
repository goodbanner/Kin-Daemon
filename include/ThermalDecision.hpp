#pragma once
#include <algorithm>

namespace kin {
inline int thermalDecisionC(int junction, int skin, bool gate,
                            int veto, int skinLimit, int junctionHard) {
    if (!gate || skin <= 5 || skin >= 70) return junction;
    int decision = skin + (veto - skinLimit);
    if (junctionHard > 0 && junction >= 0 && junction >= junctionHard)
        decision = std::max(decision, veto);
    return decision;
}

// 两个热源是否"有争议"(移植上游 ASB V64 asb_fsm.h 的 source_confidence 语义)。
//
// 上游的做法是给控制源一个 0/1/2 的置信度, 置信度 1 时负载驱动的状态迁移多要一拍确认,
// 热升档不受影响 —— "对传感器的不确定只该买到延迟, 不该买到安全"。
//
// Kin 没有独立的置信度字段, 但它的热决策本身就会暴露矛盾: 皮肤锚定决策温偏低(认为还没到
// veto), 而结温已到/超过 junction_hard_temp 并因此把决策温硬抬到 veto。这就是"两源打架",
// 此时按争议处理。只有单一热源(没有外壳传感器 / 皮肤闸门关闭)不算争议 —— 那是缺失, 不是矛盾,
// 上游同样不对缺失加罚。
inline bool thermalSourcesDisputed(int junction, int skin, bool gate,
                                   int veto, int skinLimit, int junctionHard) {
    if (!gate || skin <= 5 || skin >= 70) return false;   // 单一热源: 无矛盾可言
    if (junctionHard <= 0 || junction < 0) return false;
    if (junction < junctionHard) return false;            // 结温侧没到硬顶
    return skin + (veto - skinLimit) < veto;              // 皮肤侧仍认为未到 veto
}
}
