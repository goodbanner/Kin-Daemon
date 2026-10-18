// 热源"争议"判定的单测(移植上游 V64 source_confidence 语义)。
// 关键: 只有两个热源都在、且结论相反才算争议; 单一热源(没有外壳传感器)是缺失, 不算争议 ——
// 否则没有皮肤传感器的设备会平白多一拍延迟。
#include "ThermalDecision.hpp"
#include <cstdio>

namespace {
constexpr int kVeto = 60;        // veto_temp
constexpr int kSkinLimit = 47;   // skin_temp
constexpr int kJunctionHard = 100;
bool eq(bool got, bool want, const char* what) {
    if (got == want) return true;
    std::printf("FAIL %s: got %d want %d\n", what, (int)got, (int)want);
    return false;
}
}  // namespace

int main() {
    // 两源一致: 皮肤凉(决策温 38) + 结温也凉 → 无争议。
    if (!eq(kin::thermalSourcesDisputed(50, 25, true, kVeto, kSkinLimit, kJunctionHard), false, "both cool")) return 1;

    // 两源一致: 皮肤热(决策温 68) + 结温到硬顶 → 无争议(皮肤侧也认为到 veto 了)。
    if (!eq(kin::thermalSourcesDisputed(100, 55, true, kVeto, kSkinLimit, kJunctionHard), false, "both hot")) return 2;

    // 真争议: 皮肤侧算 38℃(未到 veto), 结温已到硬顶 → 结论相反。
    if (!eq(kin::thermalSourcesDisputed(100, 25, true, kVeto, kSkinLimit, kJunctionHard), true, "skin cool, junction hard")) return 3;
    if (!eq(kin::thermalSourcesDisputed(120, 30, true, kVeto, kSkinLimit, kJunctionHard), true, "junction above hard")) return 4;

    // 结温没到硬顶 → 皮肤侧说了算, 不算争议。
    if (!eq(kin::thermalSourcesDisputed(99, 25, true, kVeto, kSkinLimit, kJunctionHard), false, "junction under hard")) return 5;

    // 单一热源 = 缺失, 不是争议。
    if (!eq(kin::thermalSourcesDisputed(100, -1, true, kVeto, kSkinLimit, kJunctionHard), false, "no skin sensor")) return 6;
    if (!eq(kin::thermalSourcesDisputed(100, 0, true, kVeto, kSkinLimit, kJunctionHard), false, "skin zero")) return 7;
    if (!eq(kin::thermalSourcesDisputed(100, 25, false, kVeto, kSkinLimit, kJunctionHard), false, "skin gate off")) return 8;
    // 皮肤读数是垃圾值(>70)同样按缺失处理。
    if (!eq(kin::thermalSourcesDisputed(100, 85, true, kVeto, kSkinLimit, kJunctionHard), false, "skin garbage")) return 9;

    // 硬顶未配置 / 结温未知 → 不判争议(不猜)。
    if (!eq(kin::thermalSourcesDisputed(100, 25, true, kVeto, kSkinLimit, 0), false, "no junction hard limit")) return 10;
    if (!eq(kin::thermalSourcesDisputed(-1, 25, true, kVeto, kSkinLimit, kJunctionHard), false, "junction unknown")) return 11;

    // 与 thermalDecisionC 的硬顶分支必须一致: 争议成立时决策温一定被抬到 veto。
    for (int junction = 90; junction <= 120; junction += 5) {
        for (int skin = 20; skin <= 60; skin += 5) {
            const bool disputed = kin::thermalSourcesDisputed(junction, skin, true, kVeto, kSkinLimit, kJunctionHard);
            const int decision = kin::thermalDecisionC(junction, skin, true, kVeto, kSkinLimit, kJunctionHard);
            const bool overridden = (decision == kVeto && skin + (kVeto - kSkinLimit) < kVeto);
            if (disputed != overridden) {
                std::printf("FAIL disagreement with decisionC at junction=%d skin=%d (disputed=%d decision=%d)\n",
                            junction, skin, (int)disputed, decision);
                return 12;
            }
        }
    }

    std::puts("PASS thermal decision: dispute only when both sources disagree");
    return 0;
}
