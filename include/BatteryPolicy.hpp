#pragma once

namespace kin {
// Extracted low-battery hysteresis used by AutoMode; does not read hardware.
class BatteryPolicy {
public:
    void update(int percent, bool charging, int lowThreshold) {
        // Charging is an independent release signal, even if capacity cannot be read.
        if (charging) { engaged_ = false; return; }
        if (percent < 0 || percent > 100) return;
        if (!engaged_) {
            if (percent <= lowThreshold && !charging) engaged_ = true;
        } else if (percent >= 40 || charging) {
            engaged_ = false;
        }
    }
    bool engaged() const { return engaged_; }
    const char* state(int percent, bool charging, bool enabled, int criticalThreshold) const {
        if (!enabled) return "disabled";
        if (charging) return "charging";
        if (percent < 0 || percent > 100) return engaged_ ? "low_held_unknown" : "unknown";
        if (!engaged_) return "normal";
        return percent <= criticalThreshold ? "critical" : "low";
    }
    int alphaFloor(int percent, bool charging, bool enabled, int criticalThreshold) const {
        if (!enabled || !engaged_ || charging) return 0;
        return percent >= 0 && percent <= criticalThreshold && !charging ? 900 : 800;
    }
private:
    bool engaged_ = false;
};
} // namespace kin
