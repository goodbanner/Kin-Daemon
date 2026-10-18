// 智能模式会话学习方向信号的单测。
// 重点断言"方向"和上游 V64 的皮肤温加权: 学反了就是"越热越放开", 比不学更糟。
#include "SmartLearning.hpp"
#include <cstdio>

namespace {
const kin::SmartLearnWeights kDefaults{};
bool eq(int got, int want, const char* what) {
    if (got == want) return true;
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    return false;
}
}  // namespace

int main() {
    kin::SmartSessionSignals s;

    // 空会话: 没有方向信号, 不该无端漂移。
    if (!eq(kin::smartDtAlpha(s, kDefaults), 0, "empty session")) return 1;

    // 热信号方向: 热必须往省电推(正), 极热加得更多, 且不与 hot 叠加。
    s.maxTempC = 70;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 40, "hot")) return 2;
    s.maxTempC = 79;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 40, "hot upper")) return 3;
    s.maxTempC = 80;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 80, "very hot")) return 4;

    // 干净凉爽会话是唯一负向项(允许往均衡松)。
    s = kin::SmartSessionSignals{};
    s.maxTempC = 40;
    s.cleanCool = true;
    if (!eq(kin::smartDtAlpha(s, kDefaults), -60, "clean cool")) return 5;

    // 其余信号可叠加。
    s = kin::SmartSessionSignals{};
    s.maxTempC = 72;
    s.drainy = true;
    s.sustainedHeavy = true;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 40 + 30 + 20, "additive")) return 6;

    // ── 上游 V64: 热否决按皮肤温加权 ──
    // 无物理外壳传感器(-1): 必须保持改动前的满推力。
    s = kin::SmartSessionSignals{};
    s.maxTempC = 65;
    s.thermalHit = true;
    s.maxSkinC = -1;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 100, "thermal hit, no skin sensor")) return 7;

    // 皮肤确实热(≥40℃): 满推力不变。
    s.maxSkinC = 40;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 100, "thermal hit, warm skin")) return 8;
    s.maxSkinC = 46;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 100, "thermal hit, hot skin")) return 9;

    // 0 = 上游同样视为"没有传感器"(不是 0℃), 必须走满推力分支。
    s.maxSkinC = 0;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 100, "thermal hit, zero skin reads as absent")) return 15;

    // 皮肤仍凉(<40℃): 散热吃住了, 只加一半推力。
    s.maxSkinC = 39;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 50, "thermal hit, cool skin")) return 10;
    s.maxSkinC = 33;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 50, "thermal hit, very cool skin")) return 11;

    // 皮肤凉不改变其它信号: 热温 + 皮肤凉 = hot(+40) + 减半热否决(+50)。
    s = kin::SmartSessionSignals{};
    s.maxTempC = 72;
    s.thermalHit = true;
    s.maxSkinC = 35;
    if (!eq(kin::smartDtAlpha(s, kDefaults), 40 + 50, "hot + cool skin hit")) return 12;

    // 权重可配置: 把减半值设成满值即完全回到旧行为(用户/机型可退)。
    kin::SmartLearnWeights w{};
    w.thermalHitSkinCool = w.thermalHit;
    s = kin::SmartSessionSignals{};
    s.thermalHit = true;
    s.maxSkinC = 33;
    if (!eq(kin::smartDtAlpha(s, w), 100, "configurable opt-out")) return 13;

    // 方向护栏: 任何热相关组合都不能产出负值(否则会越热越放开)。
    for (int t = 30; t <= 95; t += 5) {
        for (int sk = -1; sk <= 50; sk += 5) {
            kin::SmartSessionSignals h;
            h.maxTempC = t;
            h.maxSkinC = sk;
            h.thermalHit = true;
            h.drainy = true;
            h.sustainedHeavy = true;
            if (kin::smartDtAlpha(h, kDefaults) <= 0) {
                std::printf("FAIL thermal direction at temp=%d skin=%d\n", t, sk);
                return 14;
            }
        }
    }

    std::puts("PASS smart learning: direction, additive signals, skin-weighted thermal hit");
    return 0;
}
