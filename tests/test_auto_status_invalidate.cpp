// 主机回归: 离开 auto 写恰好一帧失效快照, 之后停更; 再进入 auto 恢复 resolved=fsm。
// 模拟调度边沿: armAutoStatus = 进入 auto, writeFsmStatus = auto tick,
// writeInactiveStatus = applyCurrentMode 看到 wasAuto 后的那一次采样写入。
#include "host_posix_shim.hpp"
#include "AutoMode.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

struct AutoModeStatusTest {
    static void setStatusPath(AutoModeCtl& ctl, const std::string& path) { ctl.statusPath_ = path; }
    static const std::string& statusPath(const AutoModeCtl& ctl) { return ctl.statusPath_; }
    static const char* defaultPath() { return AutoModeCtl::kStatusPath; }
};

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 返回命中次数; 命中一次时写出值。缺字段与空值都算失败(本协议字段都应有值)。
int fieldValue(const std::string& body, const char* key, std::string& out) {
    const std::string prefix = std::string(key) + "=";
    int hits = 0;
    out.clear();
    size_t pos = 0;
    while (pos < body.size()) {
        size_t end = body.find('\n', pos);
        if (end == std::string::npos) end = body.size();
        const std::string line = body.substr(pos, end - pos);
        if (line.compare(0, prefix.size(), prefix) == 0) {
            ++hits;
            out = line.substr(prefix.size());
        }
        if (end == body.size()) break;
        pos = end + 1;
    }
    return hits;
}

int expectEq(const std::string& body, const char* key, const char* want, int code) {
    std::string got;
    const int hits = fieldValue(body, key, got);
    if (hits != 1 || got != want) {
        std::printf("FAIL %s: hits=%d got='%s' want='%s'\n", key, hits, got.c_str(), want);
        return code;
    }
    return 0;
}

AutoModeCtl::Metrics metrics(int batt, int curMa, int temp, int bucket = 6) {
    AutoModeCtl::Metrics m;
    m.battPct = batt;
    m.currentMa = curMa;
    m.tempC = temp;
    m.bucket = bucket;
    m.daypart = 3;
    m.screenOn = 1;
    m.gpuPct = 7;
    m.load1 = 0.5f;
    m.cpuBusy = 11;
    m.powerW = 1;
    m.charging = false;
    return m;
}

}  // namespace

int main() {
    const std::string dir = "/tmp/kin-auto-status-invalidate";
    const std::string path = dir + "/auto_status.txt";
    mkdir(dir.c_str(), 0755);
    unlink(path.c_str());

    Logger logger;
    AutoModeCtl ctl(logger);
    if (AutoModeStatusTest::statusPath(ctl) != AutoModeStatusTest::defaultPath()) {
        std::puts("FAIL production auto_status path changed");
        return 1;
    }
    AutoModeStatusTest::setStatusPath(ctl, path);

    kin::PolicyDecision live;
    live.policy = "fsm";
    live.reason = "state";

    // 尚未进入 auto: tick 不得创建/刷新状态文件。
    ctl.writeFsmStatus(metrics(72, 305, 41), 400, "MODERATE", 1800, 2400, live, false, false);
    if (!readFile(path).empty()) {
        std::puts("FAIL status written before auto was armed");
        return 2;
    }

    ctl.armAutoStatus();
    ctl.writeFsmStatus(metrics(72, 305, 41), 400, "MODERATE", 1800, 2400, live, false, false);
    const std::string liveFrame = readFile(path);
    if (liveFrame.empty()) {
        std::puts("FAIL live frame missing");
        return 3;
    }
    if (int rc = expectEq(liveFrame, "resolved", "fsm", 4)) return rc;
    if (int rc = expectEq(liveFrame, "fsm", "MODERATE", 5)) return rc;
    if (int rc = expectEq(liveFrame, "cpu_max_little", "1800", 6)) return rc;
    if (int rc = expectEq(liveFrame, "cpu_max_big", "2400", 7)) return rc;
    if (int rc = expectEq(liveFrame, "batt", "72", 8)) return rc;
    if (int rc = expectEq(liveFrame, "cur_ma", "305", 9)) return rc;
    if (int rc = expectEq(liveFrame, "temp", "41", 10)) return rc;
    if (int rc = expectEq(liveFrame, "event", "steady", 11)) return rc;
    if (int rc = expectEq(liveFrame, "seq", "1", 12)) return rc;
    if (int rc = expectEq(liveFrame, "schema_version", "2", 13)) return rc;
    if (int rc = expectEq(liveFrame, "owner", "kin_daemon", 14)) return rc;
    if (int rc = expectEq(liveFrame, "resolved_policy", "fsm", 15)) return rc;

    // 1Hz 节流仍在: 紧接着的 auto tick 不得改写 caps。
    ctl.writeFsmStatus(metrics(70, 100, 40), 100, "GAMING", 1900, 2500, live, true, false);
    if (readFile(path) != liveFrame) {
        std::puts("FAIL live throttle rewrote auto_status");
        return 16;
    }

    const long long t0 = (long long)time(nullptr);
    ctl.writeInactiveStatus(metrics(64, 120, 32), "balance");
    const long long t1 = (long long)time(nullptr);
    const std::string inactive = readFile(path);
    if (inactive.empty() || inactive == liveFrame) {
        std::puts("FAIL leave-auto did not publish an invalidate frame");
        return 17;
    }
    if (int rc = expectEq(inactive, "resolved", "balance", 18)) return rc;
    if (int rc = expectEq(inactive, "fsm", "INACTIVE", 19)) return rc;
    if (int rc = expectEq(inactive, "batt", "64", 20)) return rc;
    if (int rc = expectEq(inactive, "cur_ma", "120", 21)) return rc;
    if (int rc = expectEq(inactive, "temp", "32", 22)) return rc;
    if (int rc = expectEq(inactive, "cpu_max_little", "-1", 23)) return rc;
    if (int rc = expectEq(inactive, "cpu_max_big", "-1", 24)) return rc;
    if (int rc = expectEq(inactive, "alpha", "-1", 25)) return rc;
    if (int rc = expectEq(inactive, "event", "inactive", 26)) return rc;
    if (int rc = expectEq(inactive, "seq", "2", 27)) return rc;
    if (int rc = expectEq(inactive, "schema_version", "2", 28)) return rc;
    if (int rc = expectEq(inactive, "owner", "kin_daemon", 29)) return rc;
    if (int rc = expectEq(inactive, "fas_state", "inactive", 30)) return rc;
    if (int rc = expectEq(inactive, "battery_state", "normal", 31)) return rc;
    if (int rc = expectEq(inactive, "reason", "smart_policy", 32)) return rc;
    std::string tsText;
    if (fieldValue(inactive, "timestamp", tsText) != 1) {
        std::puts("FAIL timestamp missing");
        return 33;
    }
    const long long ts = std::atoll(tsText.c_str());
    if (ts < t0 || ts > t1) {
        std::printf("FAIL timestamp %lld not in [%lld, %lld]\n", ts, t0, t1);
        return 34;
    }

    // 离开之后的 tick, 以及再调用一次失效写入, 都不得刷新 FSM caps / 遥测。
    ctl.writeFsmStatus(metrics(10, 999, 90), 900, "GAMING", 3000, 3500, live, true, false);
    ctl.writeInactiveStatus(metrics(1, 1, 1), "powersave");
    ctl.writeFsmStatus(metrics(2, 2, 2), 1, "HEAVY", 1111, 2222, live, false, false);
    if (readFile(path) != inactive) {
        std::puts("FAIL non-auto ticks kept refreshing auto_status");
        return 35;
    }

    // 回到 auto: 恢复正常快照, 且不被离开前的节流挡住。
    ctl.armAutoStatus();
    ctl.writeFsmStatus(metrics(63, 200, 33), 420, "LIGHT_IDLE", 1500, 2100, live, false, false);
    const std::string resumed = readFile(path);
    if (resumed == inactive) {
        std::puts("FAIL re-enter auto did not resume status");
        return 36;
    }
    if (int rc = expectEq(resumed, "resolved", "fsm", 37)) return rc;
    if (int rc = expectEq(resumed, "fsm", "LIGHT_IDLE", 38)) return rc;
    if (int rc = expectEq(resumed, "cpu_max_little", "1500", 39)) return rc;
    if (int rc = expectEq(resumed, "cpu_max_big", "2100", 40)) return rc;
    if (int rc = expectEq(resumed, "batt", "63", 41)) return rc;
    if (int rc = expectEq(resumed, "cur_ma", "200", 42)) return rc;
    if (int rc = expectEq(resumed, "temp", "33", 43)) return rc;
    if (int rc = expectEq(resumed, "event", "steady", 44)) return rc;
    if (int rc = expectEq(resumed, "seq", "3", 45)) return rc;
    if (int rc = expectEq(resumed, "resolved_policy", "fsm", 46)) return rc;

    // 再次离开: 闩锁已随重新进入清掉, 再写恰好一帧, 档名跟着新模式走。
    ctl.writeInactiveStatus(metrics(60, 80, 31), "performance");
    const std::string leftAgain = readFile(path);
    if (int rc = expectEq(leftAgain, "resolved", "performance", 47)) return rc;
    if (int rc = expectEq(leftAgain, "fsm", "INACTIVE", 48)) return rc;
    if (int rc = expectEq(leftAgain, "batt", "60", 49)) return rc;
    if (int rc = expectEq(leftAgain, "cur_ma", "80", 50)) return rc;
    if (int rc = expectEq(leftAgain, "temp", "31", 51)) return rc;
    if (int rc = expectEq(leftAgain, "cpu_max_little", "-1", 52)) return rc;
    if (int rc = expectEq(leftAgain, "seq", "4", 53)) return rc;
    ctl.writeFsmStatus(metrics(9, 9, 9), 9, "GAMING", 4000, 4000, live, false, false);
    ctl.writeInactiveStatus(metrics(8, 8, 8), "fast");
    if (readFile(path) != leftAgain) {
        std::puts("FAIL second leave kept writing");
        return 54;
    }

    // 非法档名不得注入键值, 也不得写成 resolved=fsm。
    ctl.armAutoStatus();
    ctl.writeFsmStatus(metrics(50, 50, 30), 10, "MODERATE", 1600, 2200, live, false, false);
    ctl.writeInactiveStatus(metrics(55, 40, 29), "bad\nresolved=fsm");
    const std::string sanitized = readFile(path);
    if (int rc = expectEq(sanitized, "resolved", "unknown", 55)) return rc;
    if (int rc = expectEq(sanitized, "fsm", "INACTIVE", 56)) return rc;
    if (int rc = expectEq(sanitized, "batt", "55", 57)) return rc;
    if (sanitized.find("bad") != std::string::npos || sanitized.find("resolved=fsm") != std::string::npos) {
        std::puts("FAIL mode token injected into status");
        return 58;
    }
    // 文件仍是完整快照, 不是删掉或截成空文件。
    if (sanitized.find("schema_version=2\n") == std::string::npos ||
        sanitized.find("owner=kin_daemon\n") == std::string::npos) {
        std::puts("FAIL invalidate frame dropped protocol keys");
        return 59;
    }

    std::puts("PASS auto status: one invalidate frame on leave, stop, resume on re-enter");
    return 0;
}
