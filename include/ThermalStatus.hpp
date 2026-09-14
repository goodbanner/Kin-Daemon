#pragma once

#include <string_view>

namespace kin {

// Diagnostic classification only; it never replaces the AutoFsm thermal decision.
inline std::string_view thermalStatus(int decisionTempC, int skinC, int headroom,
                                      int headroomValid, int hardClamp) noexcept {
    if (hardClamp || (headroomValid && headroom >= 0 && headroom < 20)) return "hard_clamp";
    if (decisionTempC >= 0 && decisionTempC >= 60) return "critical";
    if (decisionTempC >= 0 && decisionTempC >= 50) return "hot";
    if (skinC > 5 && skinC < 70 && skinC >= 45) return "warm_skin";
    if (headroomValid && headroom >= 0 && headroom < 50) return "vendor_limited";
    if (decisionTempC < 0 && skinC <= 5 && !headroomValid) return "unknown";
    return "normal";
}

} // namespace kin
