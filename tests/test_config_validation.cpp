#include "ConfigValidation.hpp"
#include <cstdio>
#include <initializer_list>
int main() {
    kin::PolicyEntry app{-1, 101, -2, {0,2,0,1,9,-1,0,1}};
    kin::validatePolicyEntry(app, false);
    if (app.maxL != 100 || app.maxB != 100 || app.gpu != 0 || app.cores[0] != 1 || app.cores[1] != 1) return 4;
    kin::PolicyEntry fsm{5, 101, 150, {0,0,0,0,0,0,0,0}};
    kin::validatePolicyEntry(fsm, true);
    if (fsm.maxL != -1 || fsm.maxB != -1 || fsm.gpu != 0 || fsm.cores[0] != 1) return 5;
    kin::AutoModeConfigValues v{-1, 99, 20, 30, 5, 5, 200, 31, -1, 100,
                                10, 200, 2000, -1, 101, 1, 0, 1000, 999,
                                -1.0f, 5.0f, 40.0f, 24, -1, 1, 1001, 0,
                                20, 20, 60, 60, 30};
    kin::validateAutoModeConfig(v);
    if (v.lowBatt != 20 || v.critBatt != 10 || v.thermalEnter != 54 || v.thermalExit != 48 ||
        v.antiSoft != 5 || v.antiHard != 4 || v.attackTemp != 46 || v.explorePct != 4 ||
        v.exploreOffset != 60 || v.rewardStep != 8 || v.cameraPct != 80 ||
        v.gameUclamp != 20 || v.boostValue != 128 || v.boostPsi != 20 ||
        v.boostBusy != 85 || v.boostCapPct != 100 || v.throttleBigPct != 80 ||
        v.throttleCriticalPct != 70 || v.fasTargetFps != 0 ||
        v.throttleW != 7.5f || v.throttleCriticalW != 9.5f || v.boostJankLoad != 4.0f ||
        v.nightStart != 23 || v.nightEnd != 6 || v.tickSec != 2 || v.learnRate != 80 ||
        v.drainHigh != 14 || v.warmTemp != 50 || v.coolTemp != 55 || v.hotTemp != 60 ||
        v.veryHotTemp != 80 || v.vetoTemp != 60) return 1;
    v = {20, 10, 60, 50, 80, 40, 55, 10, 100, 20,
         80, 20, 128, 20, 85, 100, 80, 60, 120, 7.5f, 9.5f, 4.0f,
         23, 6, 2, 80, 14, 50, 55, 70, 80, 60};
    kin::validateAutoModeConfig(v);
    if (v.lowBatt != 20 || v.critBatt != 10 || v.thermalExit != 50 || v.antiHard != 40) return 2;
    v.throttleW = 12.0f; v.throttleCriticalW = 10.0f; v.boostJankLoad = -2.0f;
    kin::validateAutoModeConfig(v);
    if (v.throttleW != 12.0f || v.throttleCriticalW != 14.0f || v.boostJankLoad != 4.0f) return 3;
    for (int boundary : {1, 30, 39, 40, 95, 100, 120}) {
        kin::AutoModeConfigValues edge;
        edge.lowBatt = boundary;
        edge.antiSoft = boundary;
        edge.thermalEnter = boundary;
        edge.thermalExit = boundary;
        edge.throttleW = static_cast<float>(boundary);
        edge.throttleCriticalW = 0;
        edge.warmTemp = boundary;
        edge.coolTemp = boundary;
        edge.hotTemp = boundary;
        edge.veryHotTemp = boundary;
        kin::validateAutoModeConfig(edge);
        if (!(edge.critBatt < edge.lowBatt && edge.lowBatt < 40 &&
              edge.antiHard < edge.antiSoft && edge.thermalExit >= 25 &&
              edge.thermalExit < edge.thermalEnter && edge.throttleCriticalW <= 30 &&
              edge.throttleCriticalW > edge.throttleW && edge.warmTemp < edge.coolTemp &&
              edge.coolTemp <= 95 && edge.coolTemp < edge.hotTemp &&
              edge.hotTemp < edge.veryHotTemp && edge.veryHotTemp <= 120)) {
            std::printf("FAIL repaired configuration violates invariant at %d\n", boundary);
            return 6;
        }
    }
    std::puts("PASS config validation: ranges and dependent thresholds");
}
