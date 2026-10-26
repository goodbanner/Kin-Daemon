// AC-5: 非 auto 前台帧使用 App 约定的键。离开 auto 的失效闩不被这帧拆掉, 1Hz 也不恢复。
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
};

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

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

AutoModeCtl::Metrics metrics(int batt, int curMa, int temp) {
    AutoModeCtl::Metrics m;
    m.battPct = batt;
    m.currentMa = curMa;
    m.tempC = temp;
    m.bucket = 6;
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
    char dirTemplate[] = "/tmp/kin-manual-status-XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) return 1;
    const std::string path = std::string(dir) + "/auto_status.txt";

    Logger logger;
    AutoModeCtl ctl(logger);
    AutoModeStatusTest::setStatusPath(ctl, path);
    kin::PolicyDecision decision;

    ctl.armAutoStatus();
    ctl.writeFsmStatus(metrics(80, 400, 36), 500, "GAMING", 1800, 2400, decision, false, false);
    const std::string live = readFile(path);
    if (expectEq(live, "fsm", "GAMING", 2)) return 2;
    ctl.writeManualForeground("balance", "com.tencent.tmgp.sgame", "game_heavy", "fengchi_takeover", 1);
    if (readFile(path) != live) {
        std::printf("FAIL live frame changed after manual write\n");
        return 3;
    }

    ctl.writeInactiveStatus(metrics(77, 510, 41), "balance");
    const std::string inactive = readFile(path);
    if (expectEq(inactive, "fsm", "INACTIVE", 4)) return 4;
    if (expectEq(inactive, "resolved", "balance", 5)) return 5;
    if (expectEq(inactive, "batt", "77", 6)) return 6;
    if (expectEq(inactive, "cur_ma", "510", 7)) return 7;
    if (expectEq(inactive, "temp", "41", 8)) return 8;
    if (expectEq(inactive, "cpu_max_little", "-1", 9)) return 9;
    if (expectEq(inactive, "alpha", "-1", 10)) return 10;

    ctl.writeManualForeground("balance", "com.tencent.tmgp.sgame", "game_heavy", "fengchi_takeover", 1);
    const std::string fengchi = readFile(path);
    if (fengchi == inactive) {
        std::printf("FAIL manual frame was not written\n");
        return 11;
    }
    if (expectEq(fengchi, "fsm", "INACTIVE", 12)) return 12;
    if (expectEq(fengchi, "mode", "balance", 13)) return 13;
    if (expectEq(fengchi, "resolved", "balance", 14)) return 14;
    if (expectEq(fengchi, "fg_pkg", "com.tencent.tmgp.sgame", 15)) return 15;
    if (expectEq(fengchi, "profile", "game_heavy", 16)) return 16;
    if (expectEq(fengchi, "policy_reason", "fengchi_takeover", 17)) return 17;
    if (expectEq(fengchi, "oiface", "1", 18)) return 18;
    if (expectEq(fengchi, "cpu_max_little", "-1", 19)) return 19;
    if (expectEq(fengchi, "cpu_max_big", "-1", 20)) return 20;
    if (expectEq(fengchi, "alpha", "-1", 21)) return 21;
    if (expectEq(fengchi, "batt", "-1", 22)) return 22;
    if (expectEq(fengchi, "cur_ma", "-1", 23)) return 23;
    if (expectEq(fengchi, "temp", "-1", 24)) return 24;
    if (expectEq(fengchi, "event", "foreground", 25)) return 25;
    if (expectEq(fengchi, "reason", "manual_foreground", 26)) return 26;

    ctl.writeManualForeground("balance", "com.tencent.tmgp.sgame", "game_heavy", "fengchi_takeover", 1);
    if (readFile(path) != fengchi) {
        std::printf("FAIL dedup rewrote the same foreground\n");
        return 27;
    }

    ctl.writeManualForeground("balance", "com.android.launcher3", "", "global", 0);
    const std::string left = readFile(path);
    if (expectEq(left, "fsm", "INACTIVE", 28)) return 28;
    if (expectEq(left, "mode", "balance", 29)) return 29;
    if (expectEq(left, "fg_pkg", "com.android.launcher3", 30)) return 30;
    if (expectEq(left, "profile", "", 31)) return 31;
    if (expectEq(left, "policy_reason", "global", 32)) return 32;
    if (expectEq(left, "oiface", "0", 33)) return 33;
    if (expectEq(left, "cpu_max_little", "-1", 34)) return 34;
    if (expectEq(left, "alpha", "-1", 35)) return 35;

    ctl.writeFsmStatus(metrics(10, 10, 10), 1, "IDLE", 100, 100, decision, true, true);
    ctl.writeInactiveStatus(metrics(11, 11, 11), "performance");
    if (readFile(path) != left) {
        std::printf("FAIL fsm/inactive writer clobbered the manual frame\n");
        return 36;
    }

    ctl.writeManualForeground("auto\nbalance", "com.foo;rm -rf", "game heavy", "bad reason", 2);
    const std::string dirty = readFile(path);
    if (expectEq(dirty, "mode", "unknown", 37)) return 37;
    if (expectEq(dirty, "resolved", "unknown", 38)) return 38;
    if (expectEq(dirty, "fg_pkg", "", 39)) return 39;
    if (expectEq(dirty, "profile", "", 40)) return 40;
    if (expectEq(dirty, "policy_reason", "unknown", 41)) return 41;
    if (expectEq(dirty, "oiface", "0", 42)) return 42;
    if (expectEq(dirty, "fsm", "INACTIVE", 43)) return 43;

    ctl.armAutoStatus();
    ctl.writeFsmStatus(metrics(70, 300, 35), 400, "IDLE", 1200, 1600, decision, false, false);
    const std::string resumed = readFile(path);
    if (expectEq(resumed, "fsm", "IDLE", 44)) return 44;
    if (expectEq(resumed, "resolved", "fsm", 45)) return 45;

    std::printf("PASS: manual foreground frames keep INACTIVE numerics and the invalidate latch\n");
    return 0;
}
