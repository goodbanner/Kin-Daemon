#pragma once

namespace kin {

// 把 current_now 的原始读数换成放电电流绝对值(mA)。
//
// 内核约定是 µA, 少数机型直接写 mA。旧实现用「第一拍 >=100000 才是 µA, 否则永久锁成 mA」。
// 开机/息屏第一拍经常是 0 或 <100mA(µA 下即 <100000), 于是被锁成 mA:
//   - 之后真正的 300000µA(300mA) 被当成 300000mA, 再被 >100000 的合理性钳位变成 0;
//   - 电流佐证和功率压制整段失效, 直到进程重启。
// 这里只在出现不可能是 mA 的读数(>=100000)时把这一拍按 µA 换算; 连续两拍才锁定。
// 锁定之后, 待机时的小读数也 ÷1000。一次毛刺不会把 mA 机型永久锁死。
// 0 不推进也不清掉锁定计数(BMS 未就绪时常读到 0)。
// 换算后仍 >100000mA 的值按旧语义归零。
inline int batteryCurrentMa(long raw, int& unit, int& largeStreak) {
    long magnitude = raw < 0 ? -raw : raw;
    if (magnitude >= 100000L) {
        if (largeStreak < 1000000) ++largeStreak;
        if (largeStreak >= 2) unit = 1;
        magnitude /= 1000;
    } else if (unit == 1) {
        magnitude /= 1000;
    } else if (magnitude > 0) {
        largeStreak = 0;
    }
    if (magnitude > 100000L) return 0;
    return static_cast<int>(magnitude);
}

}  // namespace kin
