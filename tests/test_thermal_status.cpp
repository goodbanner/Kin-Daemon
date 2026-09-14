#include "ThermalStatus.hpp"
#include <cstdio>
#include <string_view>

int main() {
    using namespace std::literals;
    if (kin::thermalStatus(80, 40, -1, 0, 1) != "hard_clamp"sv) return 1;
    if (kin::thermalStatus(40, 40, 10, 1, 0) != "hard_clamp"sv) return 2;
    if (kin::thermalStatus(60, 40, 80, 1, 0) != "critical"sv) return 3;
    if (kin::thermalStatus(52, 40, 80, 1, 0) != "hot"sv) return 4;
    if (kin::thermalStatus(42, 47, 80, 1, 0) != "warm_skin"sv) return 5;
    if (kin::thermalStatus(42, 40, 40, 1, 0) != "vendor_limited"sv) return 6;
    if (kin::thermalStatus(-1, 5, -1, 0, 0) != "unknown"sv) return 7;
    if (kin::thermalStatus(42, 40, 80, 1, 0) != "normal"sv) return 8;
    std::puts("PASS thermal status: hard, critical, hot, skin, vendor and unknown");
}
