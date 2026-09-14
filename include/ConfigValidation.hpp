#pragma once
#include <algorithm>

namespace kin {
struct PolicyEntry {
    int maxL = -1;
    int maxB = -1;
    int gpu = 0;
    int cores[8] = {1,1,1,1,1,1,1,1};
};

inline void validatePolicyEntry(PolicyEntry& p, bool fsmPolicy) noexcept {
    if (p.maxL < 10 || p.maxL > 100) p.maxL = fsmPolicy ? -1 : 100;
    if (p.maxB < 10 || p.maxB > 100) p.maxB = fsmPolicy ? -1 : 100;
    if (p.gpu < 0 || p.gpu > 100) p.gpu = 0;
    for (int& core : p.cores) core = core == 0 ? 0 : 1;
    if (p.cores[0] == 0) p.cores[0] = 1;
}

struct AutoModeConfigValues {
    int lowBatt = 20;
    int critBatt = 10;
    int thermalEnter = 54;
    int thermalExit = 48;
    int antiSoft = 70;
    int antiHard = 45;
    int attackTemp = 46;
    int explorePct = 4;
    int exploreOffset = 60;
    int rewardStep = 8;
    int cameraPct = 80;
    int gameUclamp = 20;
    int boostValue = 128;
    int boostPsi = 20;
    int boostBusy = 85;
    int boostCapPct = 100;
    int throttleBigPct = 80;
    int throttleCriticalPct = 60;
    int fasTargetFps = 0;
    float throttleW = 7.5f;
    float throttleCriticalW = 9.5f;
    float boostJankLoad = 4.0f;
    int nightStart = 23;
    int nightEnd = 6;
    int tickSec = 2;
    int learnRate = 80;
    int drainHigh = 14;
    int warmTemp = 50;
    int coolTemp = 55;
    int hotTemp = 70;
    int veryHotTemp = 80;
    int vetoTemp = 60;
};

inline void validateAutoModeConfig(AutoModeConfigValues& v) noexcept {
    if (v.lowBatt < 1 || v.lowBatt >= 40) v.lowBatt = 20;
    if (v.critBatt < 0 || v.critBatt >= v.lowBatt) v.critBatt = 10 < v.lowBatt ? 10 : v.lowBatt - 1;
    const bool invalidThermalEnter = v.thermalEnter < 30 || v.thermalEnter > 95;
    if (invalidThermalEnter) v.thermalEnter = 54;
    if (invalidThermalEnter || v.thermalExit < 25 || v.thermalExit >= v.thermalEnter)
        v.thermalExit = std::max(25, v.thermalEnter - 6);
    if (v.antiSoft < 2 || v.antiSoft > 100) v.antiSoft = 70;
    if (v.antiHard < 1 || v.antiHard >= v.antiSoft) v.antiHard = v.antiSoft > 1 ? v.antiSoft - 1 : 1;
    if (v.attackTemp < 30 || v.attackTemp > 95) v.attackTemp = 46;
    if (v.explorePct < 0 || v.explorePct > 30) v.explorePct = 4;
    if (v.exploreOffset < 0 || v.exploreOffset > 200) v.exploreOffset = 60;
    if (v.rewardStep < 0 || v.rewardStep > 50) v.rewardStep = 8;
    if (v.cameraPct < 40 || v.cameraPct > 100) v.cameraPct = 80;
    if (v.gameUclamp < 0 || v.gameUclamp > 100) v.gameUclamp = 20;
    if (v.boostValue < 0 || v.boostValue > 1024) v.boostValue = 128;
    if (v.boostPsi < 0 || v.boostPsi > 100) v.boostPsi = 20;
    if (v.boostBusy < 0 || v.boostBusy > 100) v.boostBusy = 85;
    if (v.boostCapPct < 10 || v.boostCapPct > 100) v.boostCapPct = 100;
    if (v.throttleBigPct < 10 || v.throttleBigPct > 100) v.throttleBigPct = 80;
    if (v.throttleCriticalPct < 10 || v.throttleCriticalPct > v.throttleBigPct)
        v.throttleCriticalPct = v.throttleBigPct > 10 ? v.throttleBigPct - 10 : 10;
    if (v.fasTargetFps < 0 || v.fasTargetFps > 360) v.fasTargetFps = 0;
    if (!(v.throttleW > 0.0f) || v.throttleW >= 30.0f) v.throttleW = 7.5f;
    if (!(v.throttleCriticalW > v.throttleW) || v.throttleCriticalW > 30.0f)
        v.throttleCriticalW = std::min(30.0f, v.throttleW + 2.0f);
    if (!(v.boostJankLoad >= 0.0f) || v.boostJankLoad > 32.0f) v.boostJankLoad = 4.0f;
    if (v.nightStart < 0 || v.nightStart > 23) v.nightStart = 23;
    if (v.nightEnd < 0 || v.nightEnd > 23) v.nightEnd = 6;
    if (v.tickSec < 2 || v.tickSec > 120) v.tickSec = 2;
    if (v.learnRate < 0 || v.learnRate > 1000) v.learnRate = 80;
    if (v.drainHigh < 1 || v.drainHigh > 100) v.drainHigh = 14;
    if (v.warmTemp < 30 || v.warmTemp > 95) v.warmTemp = 50;
    if (v.warmTemp >= 95) v.warmTemp = 50;
    if (v.coolTemp <= v.warmTemp || v.coolTemp > 95) v.coolTemp = std::min(95, v.warmTemp + 5);
    if (v.hotTemp <= v.coolTemp || v.hotTemp > 110) v.hotTemp = std::max(70, v.coolTemp + 1);
    if (v.veryHotTemp <= v.hotTemp || v.veryHotTemp > 120) v.veryHotTemp = std::max(80, v.hotTemp + 1);
    if (v.vetoTemp < 40 || v.vetoTemp > 100) v.vetoTemp = 60;
}
} // namespace kin
