#pragma once

namespace kin {

// 一拍 CPU 结温是否采信。max1/max2 为最高与次高有效读数(℃), 没有则为 -1。
// lastGood<=0 表示还没有基线。streak 由调用方保存。
//
// 已有基线时: 单拍跳升 >=25℃ 且次高传感器不背书, 先拒绝并沿用基线。
// 无基线时: 孤立读数已经到 vhot, 同样先拒绝(返回 -1, 不播种基线)。
// 两种拒绝都只持续一拍。下一拍仍是这个高温就接受, 避免单传感器设备
// 把真实骤升或开机就已经很热的读数永久冻住, 热否决从此不再触发。
struct CpuTempGuard {
    int published;  // 本拍对外温度; -1 = 未知
    int baseline;   // 更新后的基线
    int streak;
};

inline CpuTempGuard acceptCpuTemp(int max1, int max2, int lastGood, int streak, int vhot) {
    CpuTempGuard out;
    out.baseline = lastGood;
    out.streak = streak;
    if (max1 < 0) {
        out.published = lastGood > 0 ? lastGood : -1;
        return out;
    }
    const bool isolated = (max2 < 0 || max1 >= max2 + 25);
    const bool spike = (lastGood > 0 && max1 >= lastGood + 25 && isolated);
    const bool cold = (lastGood <= 0 && max1 >= vhot && isolated);
    if (spike || cold) {
        out.streak = streak + 1;
        if (out.streak < 2) {
            out.published = lastGood > 0 ? lastGood : -1;
            return out;
        }
    }
    out.streak = 0;
    out.baseline = max1;
    out.published = max1;
    return out;
}

}  // namespace kin
