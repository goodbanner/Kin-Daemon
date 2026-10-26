// AC-4: 进入风驰后禁止 CPU 频率/governor 写入, 拉起 oiface; 保持不再进入第二次; 退出后恢复写入并还原 oiface。
#include "FengchiTakeover.hpp"

#include <cstdio>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

struct CpuSink {
    int writes = 0;
    void writeFreq(const char* what) {
        if (kin::fengchiHandsOff.load(std::memory_order_acquire) != 0) return;
        ++writes;
        (void)what;
    }
};

struct FakeOiface {
    std::string enable = "1";
    bool running = false;
    std::vector<std::string> ops;
    std::string readEnable() const { return enable; }
    bool readRunning() const { return running; }
    void setEnable(const char* v) {
        ops.push_back(std::string("set:") + (v ? v : ""));
        enable = v ? v : "";
    }
    void start() { ops.push_back("start"); running = true; }
    void stop() { ops.push_back("stop"); running = false; }
};

int countOp(const std::vector<std::string>& ops, const std::string& want) {
    int n = 0;
    for (const std::string& op : ops)
        if (op == want) ++n;
    return n;
}

}  // namespace

int main() {
    kin::fengchiHandsOff.store(0, std::memory_order_release);
    CpuSink cpu;
    cpu.writeFreq("base");
    CHECK(cpu.writes == 1);

    const kin::FengchiEdge entered = kin::stepFengchi(kin::FengchiPhase::Idle, true);
    CHECK(entered.entered);
    CHECK(entered.suppressCpuWrites);
    CHECK(entered.enableOiface);
    CHECK(!entered.exited);
    CHECK(entered.log != nullptr);
    CHECK(std::string(entered.log).find("风驰接管") != std::string::npos);
    kin::fengchiHandsOff.store(1, std::memory_order_release);
    cpu.writeFreq("during");
    CHECK(cpu.writes == 1);

    FakeOiface hw;
    hw.enable = "1\n";
    hw.running = false;
    kin::OifaceTakeover oiface;
    oiface.enter(hw);
    CHECK(oiface.engaged());
    CHECK(oiface.savedEnable() == "1");
    CHECK(!oiface.savedRunning());
    CHECK(hw.enable == "2");
    CHECK(countOp(hw.ops, "set:2") == 1);
    CHECK(countOp(hw.ops, "start") == 1);
    oiface.enter(hw);
    CHECK(countOp(hw.ops, "start") == 1);

    const kin::FengchiEdge held = kin::stepFengchi(entered.phase, true);
    CHECK(!held.entered);
    CHECK(!held.exited);
    CHECK(held.suppressCpuWrites);
    CHECK(held.phase == kin::FengchiPhase::Active);
    CHECK(held.log == nullptr);
    cpu.writeFreq("hold");
    CHECK(cpu.writes == 1);

    const kin::FengchiEdge left = kin::stepFengchi(held.phase, false);
    CHECK(left.exited);
    CHECK(left.restoreBase);
    CHECK(left.restoreOiface);
    CHECK(!left.suppressCpuWrites);
    CHECK(left.log != nullptr);
    CHECK(std::string(left.log).find("恢复基础档") != std::string::npos);
    kin::fengchiHandsOff.store(0, std::memory_order_release);
    oiface.exit(hw);
    CHECK(!oiface.engaged());
    CHECK(hw.enable == "1");
    CHECK(countOp(hw.ops, "set:1") == 1);
    CHECK(countOp(hw.ops, "stop") == 1);
    CHECK(!hw.running);
    cpu.writeFreq("restored");
    CHECK(cpu.writes == 2);

    const kin::FengchiEdge idle = kin::stepFengchi(kin::FengchiPhase::Idle, false);
    CHECK(!idle.entered);
    CHECK(!idle.exited);
    CHECK(!idle.suppressCpuWrites);

    CHECK(kin::OifaceTakeover::sanitizeEnable(" 12\n") == "12");
    CHECK(kin::OifaceTakeover::sanitizeEnable("1;reboot").empty());
    CHECK(kin::OifaceTakeover::sanitizeEnable("123456789").empty());

    FakeOiface already;
    already.enable = "0";
    already.running = true;
    kin::OifaceTakeover again;
    again.enter(already);
    again.exit(again.engaged() ? already : already);
    CHECK(already.enable == "0");
    CHECK(already.running);
    CHECK(countOp(already.ops, "start") == 2);

    std::puts("PASS: fengchi enter suppresses cpu writes, exit restores oiface and writes");
    return 0;
}
