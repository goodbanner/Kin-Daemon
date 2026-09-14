#include "BatteryPolicy.hpp"
#include <cstdio>
#include <string_view>

int main() {
    kin::BatteryPolicy policy;
    policy.update(20, false, 20);
    if (!policy.engaged() || policy.alphaFloor(20, false, true, 10) != 800) return 1;
    policy.update(10, false, 20);
    if (policy.alphaFloor(10, false, true, 10) != 900) return 2;
    policy.update(-1, false, 20);
    if (!policy.engaged() || policy.alphaFloor(-1, false, true, 10) != 800) return 3;
    policy.update(-1, true, 20);
    if (policy.engaged() || policy.alphaFloor(-1, true, true, 10) != 0) {
        std::puts("FAIL: charging must release low-battery latch even with unknown capacity");
        return 4;
    }
    policy.update(20, false, 20);
    policy.update(39, false, 20);
    if (!policy.engaged()) return 5;
    if (policy.alphaFloor(39, false, false, 10) != 0 || !policy.engaged()) return 6;
    if (policy.alphaFloor(39, false, true, 10) != 800) return 7;
    policy.update(40, false, 20);
    if (policy.engaged()) return 8;
    policy.update(20, false, 20);
    policy.update(101, false, 20);
    if (!policy.engaged()) { std::puts("FAIL: invalid capacity must not release latch"); return 9; }
    policy.update(20, true, 20);
    if (policy.engaged()) return 10;
    using namespace std::literals;
    if (policy.state(-1, true, true, 10) != "charging"sv ||
        policy.state(-1, false, true, 10) != "unknown"sv ||
        policy.state(50, false, false, 10) != "disabled"sv ||
        policy.state(50, false, true, 10) != "normal"sv) return 11;
    policy.update(10, false, 20);
    if (policy.state(10, false, true, 10) != "critical"sv ||
        policy.state(-1, false, true, 10) != "low_held_unknown"sv ||
        policy.state(25, false, true, 10) != "low"sv) return 12;
    std::puts("PASS battery policy: thresholds, hysteresis, unknown, charging, feature toggle");
}
