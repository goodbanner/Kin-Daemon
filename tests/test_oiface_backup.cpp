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
    CHECK(kin::OifaceTakeover::recoverBackup(boot, bak.c_str()));
    CHECK(boot.enable == "1");
    CHECK(!exists(bak));
    CHECK(!crashed.engaged());

    FakeOiface again;
    again.enable = "0";
    CHECK(!kin::OifaceTakeover::recoverBackup(again, bak.c_str()));
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
    CHECK(!kin::OifaceTakeover::recoverBackup(after, bak.c_str()));
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

    // 读回失败则保留文件, 下次还能还原。垃圾内容只还原成空数字串, 不会把原文 setprop 出去。
    {
        std::ofstream out(bak);
        out << "1;reboot\n";
    }
    FakeOiface dirty;
    dirty.enable = "4";
    dirty.accept = false;
    CHECK(!kin::OifaceTakeover::recoverBackup(dirty, bak.c_str()));
    CHECK(dirty.enable == "4");
    CHECK(exists(bak));
    CHECK(!dirty.ops.empty());
    CHECK(dirty.ops[0] == "set:");
    dirty.accept = true;
    dirty.ops.clear();
    CHECK(kin::OifaceTakeover::recoverBackup(dirty, bak.c_str()));
    CHECK(dirty.enable.empty());
    CHECK(!exists(bak));

    std::printf("PASS: oiface backup precedes setprop, exit deletes it, crash recover restores it\n");
    return 0;
}
