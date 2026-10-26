#include "BatteryCurrent.hpp"
#include <cstdio>

static int fails = 0;

static void step(const char* name, long raw, int& unit, int& streak, int wantMa, int wantUnit) {
    const int got = kin::batteryCurrentMa(raw, unit, streak);
    if (got == wantMa && unit == wantUnit) {
        std::printf("PASS %s -> %d unit=%d streak=%d\n", name, got, unit, streak);
        return;
    }
    std::printf("FAIL %s: ma=%d unit=%d streak=%d want ma=%d unit=%d\n",
                name, got, unit, streak, wantMa, wantUnit);
    ++fails;
}

int main() {
    int unit = 0, streak = 0;
    // 第一拍是 0(BMS 未就绪)不得锁成 mA, 下一拍真实 µA 仍要换算成 mA 而不是被钳成 0。
    step("boot zero", 0, unit, streak, 0, 0);
    step("first large ua", 300000, unit, streak, 300, 0);
    step("second large ua locks", 250000, unit, streak, 250, 1);
    step("idle after lock", 80000, unit, streak, 80, 1);
    step("negative ua", -180000, unit, streak, 180, 1);

    unit = 0;
    streak = 0;
    step("ma device light", 500, unit, streak, 500, 0);
    step("ma device load", 2400, unit, streak, 2400, 0);
    step("ma device peak", 8000, unit, streak, 8000, 0);

    unit = 0;
    streak = 0;
    step("single glitch", 999999, unit, streak, 999, 0);
    step("glitch does not lock ma device", 2200, unit, streak, 2200, 0);

    unit = 0;
    streak = 0;
    step("zero then real load", 0, unit, streak, 0, 0);
    const int load = kin::batteryCurrentMa(500000, unit, streak);
    if (load != 500 || unit != 0) {
        std::printf("FAIL zero must not force later ua to 0: ma=%d unit=%d\n", load, unit);
        ++fails;
    } else {
        std::puts("PASS zero does not zero-out a later ua sample");
    }

    if (fails) {
        std::printf("FAIL battery current: %d\n", fails);
        return 1;
    }
    std::puts("PASS battery current");
    return 0;
}
