// AC-4b: 进入接管前把原始 oiface 值原子写入备份。正常退出删除。
// 模拟崩溃: 不调用 exit, 新实例从备份还原并删除文件。
#include "FengchiTakeover.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

struct CaptureLog {
    int infos = 0;
    int warns = 0;
    std::string infoText;
    std::string warnText;
    void info(const char* message) {
        ++infos;
        infoText = message ? message : "";
    }
    void warn(const char* message) {
        ++warns;
        warnText = message ? message : "";
    }
};

struct FakeOiface {
    std::string enable = "1";
    bool running = false;
    bool accept = true;
    std::string backupPath;
    std::string backupAtSet;
    std::vector<std::string> ops;
    std::string readEnable() const { return enable; }
    bool readRunning() const { return running; }
    void setEnable(const char* v) {
        ops.push_back(std::string("set:") + (v ? v : ""));
        if (!backupPath.empty()) {
            std::ifstream in(backupPath);
            std::ostringstream ss;
            ss << in.rdbuf();
            backupAtSet = ss.str();
        }
        if (!accept) return;
        enable = v ? v : "";
    }
    void start() { ops.push_back("start"); running = true; }
    void stop() { ops.push_back("stop"); running = false; }
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

}  // namespace

int main() {
    char dirTemplate[] = "/tmp/kin-oiface-bak-XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) return 1;
    const std::string bak = std::string(dir) + "/fengchi_oiface.bak";

    FakeOiface hw;
    hw.enable = "1\n";
    hw.running = true;
    hw.backupPath = bak;
    kin::OifaceTakeover live;
    CHECK(live.enter(hw, bak.c_str()));
    CHECK(live.engaged());
    CHECK(hw.backupAtSet == "1\n");
    CHECK(readFile(bak) == "1\n");
    CHECK(hw.enable == "2");
    CHECK(hw.ops.size() >= 1);
    CHECK(hw.ops[0] == "set:2");

    // 崩溃: 不调用 exit。新进程的接管对象没有内存里的原值, 只剩备份文件。
    kin::OifaceTakeover crashed;
    FakeOiface boot;
    boot.enable = "2";
    boot.running = true;
    CaptureLog bootLog;
    CHECK(kin::OifaceTakeover::recoverBackup(boot, bak.c_str(), bootLog) == kin::OifaceBackupResult::Restored);
    CHECK(boot.enable == "1");
    CHECK(!exists(bak));
    CHECK(!crashed.engaged());
    CHECK(bootLog.infos == 1);
    CHECK(bootLog.warns == 0);
    CHECK(bootLog.infoText == kin::kOifaceBackupRestoredLog);

    FakeOiface again;
    again.enable = "0";
    CHECK(kin::OifaceTakeover::recoverBackup(again, bak.c_str()) == kin::OifaceBackupResult::Absent);
    CHECK(again.enable == "0");
    CHECK(again.ops.empty());

    // 正常退出: 还原属性并删除备份, 之后的启动恢复是空操作。
    FakeOiface normal;
    normal.enable = "1";
    normal.running = false;
    normal.backupPath = bak;
    kin::OifaceTakeover session;
    CHECK(session.enter(normal, bak.c_str()));
    CHECK(exists(bak));
    CHECK(normal.enable == "2");
    session.exit(normal);
    CHECK(!session.engaged());
    CHECK(normal.enable == "1");
    CHECK(!normal.running);
    CHECK(!exists(bak));
    FakeOiface after;
    after.enable = "7";
    CHECK(kin::OifaceTakeover::recoverBackup(after, bak.c_str()) == kin::OifaceBackupResult::Absent);
    CHECK(after.enable == "7");

    // 备份写不进去就不改属性。
    const std::string blocker = std::string(dir) + "/not-a-dir";
    {
        std::ofstream out(blocker);
        out << "x";
    }
    const std::string bad = blocker + "/fengchi_oiface.bak";
    FakeOiface blocked;
    blocked.enable = "1";
    kin::OifaceTakeover refused;
    CHECK(!refused.enter(blocked, bad.c_str()));
    CHECK(!refused.engaged());
    CHECK(blocked.enable == "1");
    CHECK(blocked.ops.empty());

    // R1: 备份已在则沿用, 不用当前属性覆盖。当前值是 5, 覆盖的话文件会变成 5。
    {
        std::ofstream out(bak);
        out << "1\n";
    }
    FakeOiface reuse;
    reuse.enable = "5";
    reuse.running = false;
    reuse.backupPath = bak;
    kin::OifaceTakeover reuseSession;
    CHECK(reuseSession.enter(reuse, bak.c_str()));
    CHECK(reuseSession.savedEnable() == "1");
    CHECK(readFile(bak) == "1\n");
    CHECK(reuse.backupAtSet == "1\n");
    CHECK(reuse.enable == "2");
    CHECK(reuse.ops[0] == "set:2");
    reuseSession.exit(reuse);
    CHECK(reuse.enable == "1");
    CHECK(!reuse.running);
    CHECK(!exists(bak));

    // R1: 启动还原 setprop/读回失败则留文件; 之后再试一次可以成功。
    {
        std::ofstream out(bak);
        out << "1\n";
    }
    FakeOiface retry;
    retry.enable = "2";
    retry.accept = false;
    CaptureLog retryLog;
    CHECK(kin::OifaceTakeover::recoverBackup(retry, bak.c_str(), retryLog) == kin::OifaceBackupResult::Kept);
    CHECK(retry.enable == "2");
    CHECK(exists(bak));
    CHECK(readFile(bak) == "1\n");
    CHECK(retry.ops.size() == 1);
    CHECK(retry.ops[0] == "set:1");
    CHECK(retryLog.warns == 0);
    CHECK(retryLog.infos == 0);
    retry.accept = true;
    retry.ops.clear();
    CHECK(kin::OifaceTakeover::recoverBackup(retry, bak.c_str(), retryLog) == kin::OifaceBackupResult::Restored);
    CHECK(retry.enable == "1");
    CHECK(!exists(bak));
    CHECK(retryLog.infos == 1);
    CHECK(retryLog.warns == 0);
    CHECK(retryLog.infoText == kin::kOifaceBackupRestoredLog);

    // R1: 启动还原失败后, 下一次退出沿用原备份再还原, 仍然不覆盖。
    {
        std::ofstream out(bak);
        out << "1\n";
    }
    FakeOiface laterExit;
    laterExit.enable = "2";
    laterExit.accept = false;
    CHECK(kin::OifaceTakeover::recoverBackup(laterExit, bak.c_str()) == kin::OifaceBackupResult::Kept);
    laterExit.accept = true;
    laterExit.running = false;
    laterExit.backupPath = bak;
    laterExit.ops.clear();
    kin::OifaceTakeover later;
    CHECK(later.enter(laterExit, bak.c_str()));
    CHECK(readFile(bak) == "1\n");
    CHECK(later.savedEnable() == "1");
    CHECK(laterExit.enable == "2");
    later.exit(laterExit);
    CHECK(laterExit.enable == "1");
    CHECK(!laterExit.running);
    CHECK(!exists(bak));

    // R1: 退出时读回失败也留着文件, 下一次启动再还原。
    FakeOiface exitFail;
    exitFail.enable = "3";
    exitFail.running = false;
    exitFail.backupPath = bak;
    kin::OifaceTakeover exitSession;
    CHECK(exitSession.enter(exitFail, bak.c_str()));
    CHECK(readFile(bak) == "3\n");
    exitFail.accept = false;
    exitSession.exit(exitFail);
    CHECK(!exitSession.engaged());
    CHECK(exitFail.enable == "2");
    CHECK(exists(bak));
    CHECK(readFile(bak) == "3\n");
    FakeOiface nextBoot;
    nextBoot.enable = "2";
    CHECK(kin::OifaceTakeover::recoverBackup(nextBoot, bak.c_str()) == kin::OifaceBackupResult::Restored);
    CHECK(nextBoot.enable == "3");
    CHECK(!exists(bak));

    // R2: 空内容和无效内容都不 setprop, 删除文件, 只打一条警告。
    auto discard = [&](const std::string& body) {
        {
            std::ofstream out(bak, std::ios::binary);
            out << body;
        }
        FakeOiface discarded;
        discarded.enable = "4";
        CaptureLog log;
        const auto result = kin::OifaceTakeover::recoverBackup(discarded, bak.c_str(), log);
        CHECK(result == kin::OifaceBackupResult::Discarded);
        CHECK(discarded.ops.empty());
        CHECK(discarded.enable == "4");
        CHECK(!exists(bak));
        CHECK(log.warns == 1);
        CHECK(log.infos == 0);
        CHECK(log.warnText == kin::kOifaceBackupInvalidLog);
        return 0;
    };
    if (discard("")) return 1;
    if (discard("\n")) return 1;
    if (discard("1;reboot\n")) return 1;

    std::printf("PASS: oiface backup is reused, failed restore retries, invalid backup is deleted\n");
    return 0;
}
