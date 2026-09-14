#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace kin {

// Pure policy result. It deliberately contains no sysfs or Android dependencies so
// policy precedence can be tested on the host and applied by one writer.
struct PolicyCaps {
    int minL = 0;
    int maxL = 0;
    int minB = 0;
    int maxB = 0;
};

struct PolicyDecision {
    PolicyCaps caps;
    std::string policy;
    std::string reason;
};

class PolicyResolver {
public:
    explicit PolicyResolver(PolicyCaps base) : base_(base), app_(base) {
        normalize(base_);
        app_ = base_;
        rebuild();
    }

    // Safety caps are retained separately, so a later app/game/FAS update cannot
    // accidentally reopen a thermal or battery restriction.
    PolicyResolver& cap(int maxL, int maxB, const char* policy, const char* reason) {
        const std::string name = policy ? policy : "cap";
        // Independent slots preserve provenance across producers. This is cap-reason
        // precedence only; hardware ownership (ROM/blacklist) stays in the scheduler.
        const size_t slot = sourceSlot(name);
        Limit& limit = limits_[slot];
        limit.priority = slot;
        // Invalid caps are not interpreted as a request to write a negative/zero frequency.
        const bool lowerL = maxL > 0 && maxL < limit.maxL;
        const bool lowerB = maxB > 0 && maxB < limit.maxB;
        if (lowerL) limit.maxL = maxL;
        if (lowerB) limit.maxB = maxB;
        if (lowerL || lowerB) {
            limit.policy = name;
            limit.reason = reason ? reason : "cap";
        }
        rebuild();
        return *this;
    }

    // Replace the candidate, not any registered cap. All producers' limits are
    // reapplied, so a later boost cannot silently undo an earlier constraint.
    PolicyResolver& appPolicy(PolicyCaps caps, const char* policy, const char* reason) {
        normalize(caps);
        if (caps.maxL <= 0) caps.maxL = base_.maxL;
        if (caps.maxB <= 0) caps.maxB = base_.maxB;
        if (caps.minL < 0) caps.minL = base_.minL;
        if (caps.minB < 0) caps.minB = base_.minB;
        if (caps.minL > caps.maxL) caps.minL = caps.maxL;
        if (caps.minB > caps.maxB) caps.minB = caps.maxB;
        app_ = caps;
        appPolicy_ = policy ? policy : "app";
        appReason_ = reason ? reason : "profile";
        rebuild();
        return *this;
    }

    const PolicyDecision& result() const { return decision_; }

private:
    static void normalize(PolicyCaps& caps) {
        if (caps.minL > caps.maxL) caps.minL = caps.maxL;
        if (caps.minB > caps.maxB) caps.minB = caps.maxB;
    }

    struct Limit {
        int maxL = std::numeric_limits<int>::max();
        int maxB = std::numeric_limits<int>::max();
        std::string policy;
        std::string reason;
        size_t priority = std::numeric_limits<size_t>::max();
    };

    void rebuild() {
        decision_.caps = app_;
        for (const auto& limit : limits_) {
            decision_.caps.maxL = std::min(decision_.caps.maxL, limit.maxL);
            decision_.caps.maxB = std::min(decision_.caps.maxB, limit.maxB);
        }
        normalize(decision_.caps);
        decision_.policy = appPolicy_;
        decision_.reason = appReason_;
        // Select a cap that actually binds a cluster. The source rank is explicit,
        // so equal caps are deterministic and independent of producer call order.
        const Limit* binding = nullptr;
        for (const auto& limit : limits_) {
            if (limit.policy.empty() ||
                !((limit.maxL <= app_.maxL && limit.maxL == decision_.caps.maxL) ||
                  (limit.maxB <= app_.maxB && limit.maxB == decision_.caps.maxB))) {
                continue;
            }
            if (!binding || limit.priority < binding->priority) binding = &limit;
        }
        if (binding) {
            decision_.policy = binding->policy;
            decision_.reason = binding->reason;
        }
    }

    static size_t sourceSlot(const std::string& name) noexcept {
        if (name == "thermal" || name == "thermal_safety") return 0;
        if (name == "battery" || name == "low_battery") return 1;
        if (name == "power") return 2;
        if (name == "app") return 3;
        if (name == "camera") return 4;
        if (name == "fas") return 5;
        return 6;
    }

    PolicyCaps base_;
    PolicyCaps app_;
    std::string appPolicy_ = "base";
    std::string appReason_ = "fsm";
    std::array<Limit, 7> limits_{};
    PolicyDecision decision_;
};

} // namespace kin
