#include "ForegroundCache.hpp"
#include <atomic>
#include <cstdio>
#include <future>
#include <thread>

struct FakeClock {
    using time_point = std::chrono::steady_clock::time_point;
    static inline time_point current{};
    static time_point now() { return current; }
    static void advance(int ms) { current += std::chrono::milliseconds(ms); }
};

int main() {
    kin::ForegroundCache<FakeClock> cache;
    int probes = 0;
    auto failed = [&] { ++probes; return kin::ForegroundSnapshot{}; };
    cache.get(0, failed);
    cache.get(0, failed);
    if (probes != 1) { std::puts("FAIL: unsuccessful probes bypass retry floor"); return 1; }
    FakeClock::advance(1999);
    cache.get(0, failed);
    if (probes != 1) return 2;
    FakeClock::advance(1);
    auto success = [&] {
        ++probes;
        return kin::ForegroundSnapshot{"com.game.app", {}, {}, true};
    };
    if (!cache.get(5000, success).valid || probes != 2) return 3;
    FakeClock::advance(4999);
    if (!cache.get(5000, failed).valid || probes != 2) return 4;
    FakeClock::advance(1);
    if (cache.get(5000, failed).valid || probes != 3) return 5;
    // A failure replaces old data and is retried after the floor, not the success TTL.
    FakeClock::advance(2000);
    if (!cache.get(5000, success).valid || probes != 4) return 6;

    kin::ForegroundCache<FakeClock> slowCache;
    auto slow = [&] { FakeClock::advance(3000); return success(); };
    slowCache.get(2000, slow);
    const int completedProbes = probes;
    slowCache.get(2000, success);
    if (probes != completedProbes) { std::puts("FAIL: TTL starts before probe completion"); return 7; }

    kin::ForegroundCache<> concurrent;
    std::atomic<int> calls{0};
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::thread first([&] { concurrent.get(0, [&] {
        ++calls;
        entered.set_value();
        gate.wait();
        return kin::ForegroundSnapshot{"com.game.app", {}, {}, true};
    }); });
    entered.get_future().wait();
    // An in-flight probe returns an invalid snapshot promptly, never spawns another probe.
    const auto second = concurrent.get(0, [&] { ++calls; return kin::ForegroundSnapshot{}; });
    release.set_value();
    first.join();
    if (calls != 1 || second.valid) { std::puts("FAIL: concurrent duplicate probe"); return 8; }
    std::puts("PASS foreground cache: failure backoff, TTL, recovery and single in-flight probe");
}
