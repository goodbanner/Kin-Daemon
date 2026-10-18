// 智能模式会话学习的方向信号(移植自 ASB asb_smart.h asb_smart_bucket_update_from_session)。
//
// 为什么抽成纯函数: 这段权重决定 alpha 的长期漂移方向 —— 正=偏省电(上限更低), 负=偏均衡。
// 一旦方向反了就是"越热越放开"的严重 bug(上游 V64 就修过一次"thermal lean 反向", 并因此重置了
// 学习模型), 而它原来内联在 AutoMode::commitSession 里, 只能靠读代码确认, 无法单测。
// 现在输入只有会话统计量、输出只有 dt_alpha, 可以逐条断言。
//
// 上游 V64 新增(本轮移植): 热否决的推力按皮肤温加权 —— 结温触发了 throttle 但皮肤仍凉, 说明散热
// 吃住了、用户没感觉, 也离 OEM 自己的 43-45℃ 皮肤上限还远, 此时只加一半推力; 皮肤确实热、或设备
// 没有物理外壳传感器(max_skin_c 未知)时保持满推力, 与改动前完全一致。
#pragma once

namespace kin {

// 一个亮屏会话的统计量。温度单位 ℃, -1 = 未知/无传感器。
struct SmartSessionSignals {
    int  maxTempC       = -1;    // 会话峰温(皮肤锚定决策温, 非原始结温)
    int  maxSkinC       = -1;    // 会话皮肤/外壳峰温; -1 = 无物理外壳传感器
    bool thermalHit     = false; // 本会话触发热否决(峰温 ≥ veto_temp)
    bool drainy         = false; // 高耗电会话(掉电率或平均放电电流超阈值)
    bool sustainedHeavy = false; // 持续重载会话
    bool cleanCool      = false; // 干净且凉爽会话(唯一负向项)
};

// 权重与阈值。默认值 = ASB V64 真源码值, 温度阈值沿用 Kin 的 Config 默认。
struct SmartLearnWeights {
    int hot          = 40;
    int vhot         = 80;
    int drainy       = 30;
    int sustained    = 20;
    int thermalHit   = 100;
    int thermalHitSkinCool = 50;   // 上游 V64: 皮肤 <40℃ 时的热否决推力(减半)
    int cleanCool    = -60;
    int hotTempC     = 70;
    int vhotTempC    = 80;
    int skinCoolC    = 40;
};

// 会话方向信号合计(未乘学习率)。调用方再按 learn_rate/1000 缩放。
inline int smartDtAlpha(const SmartSessionSignals& s, const SmartLearnWeights& w) {
    int dt = 0;
    if (s.maxTempC >= w.vhotTempC)     dt += w.vhot;
    else if (s.maxTempC >= w.hotTempC) dt += w.hot;
    if (s.drainy)                      dt += w.drainy;
    if (s.sustainedHeavy)              dt += w.sustained;
    if (s.thermalHit) {
        // 皮肤 >0 才算"有传感器": Kin 无外壳传感器时 skinC 恒 -1, 必须走满推力分支。
        const bool skinCool = (s.maxSkinC > 0 && s.maxSkinC < w.skinCoolC);
        dt += skinCool ? w.thermalHitSkinCool : w.thermalHit;
    }
    if (s.cleanCool)                   dt += w.cleanCool;
    return dt;
}

} // namespace kin
