#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace kin {
struct ForegroundSnapshot {
    std::string topPkg;
    std::vector<std::string> visible;
    std::vector<std::string> standardVisible;
    bool valid = false;
};

// Activity Task snapshots only; accessibility pushes do not pass through this cache.
template<class Clock = std::chrono::steady_clock>
class ForegroundCache {
public:
    template<class Probe>
    ForegroundSnapshot get(int ttlMs, Probe probe) {
        // Never queue behind a slow dumpsys or duplicate it. Returning unknown lets
        // callers use their existing fallback without a lock-order dependency.
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return {};
        constexpr int retryFloorMs = 2000;
        const int interval = last_.valid ? std::max(ttlMs, retryFloorMs) : retryFloorMs;
        const auto now = Clock::now();
        if (sampled_ && now >= completed_ &&
            now - completed_ < std::chrono::milliseconds(interval)) return last_;
        last_ = probe(); // Cache invalid results too; never preserve a stale successful snapshot.
        completed_ = Clock::now();
        sampled_ = true;
        return last_;
    }

private:
    std::mutex mutex_;
    ForegroundSnapshot last_;
    typename Clock::time_point completed_{};
    bool sampled_ = false;
};
} // namespace kin
