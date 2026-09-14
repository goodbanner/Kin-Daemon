#pragma once
#include <chrono>
#include <string>
#include <mutex>

namespace kin {
template<class Clock = std::chrono::steady_clock>
class PresenceCache {
public:
    template<class Probe>
    bool alive(const std::string& key, int ttlMs, Probe probe) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (key.empty()) { sampled_ = false; key_.clear(); return false; }
        const auto now = Clock::now();
        if (key == key_ && sampled_ && now >= checked_ &&
            now - checked_ < std::chrono::milliseconds(ttlMs)) return value_;
        key_ = key;
        value_ = probe(key);
        checked_ = Clock::now();
        sampled_ = true;
        return value_;
    }
private:
    std::mutex mutex_;
    std::string key_;
    typename Clock::time_point checked_{};
    bool value_ = false;
    bool sampled_ = false;
};
}
