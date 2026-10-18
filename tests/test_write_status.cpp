#include "WriteStatus.hpp"
#include <cstdio>
#include <string>
#include <thread>
int main() {
    kin::WriteStatusTracker t;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    t.record(kin::WriteStatus::Written);
    t.record(kin::WriteStatus::Failed);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Failed || t.get() != kin::WriteStatus::Failed) return 1;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Unchanged) return 2;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    std::thread unrelated([&] { t.record(kin::WriteStatus::Failed); });
    unrelated.join();
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Unchanged) {
        std::puts("FAIL unrelated worker contaminated CPU batch"); return 3;
    }
    t.startBatch();
    t.record(kin::WriteStatus::Failed);
    t.record(kin::WriteStatus::Written);
    t.record(kin::WriteStatus::Unchanged);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Failed) return 4;
    t.startBatch(); t.endBatch();
    if (t.batch() != kin::WriteStatus::Unknown) return 5;

    // 逐节点结果: 无路径的旧记录不参与节点计数。
    t.startBatch();
    t.record(kin::WriteStatus::Written);
    if (!t.batchNodeSummary().empty() || t.batchNodes().total() != 0) {
        std::puts("FAIL path-less record leaked into node counts"); return 6;
    }
    t.recordNode("/sys/devices/system/cpu/cpufreq/policy0/scaling_max", kin::WriteStatus::Written);
    t.recordNode("/sys/devices/system/cpu/cpufreq/policy1/scaling_max", kin::WriteStatus::Written);
    t.recordNode("/sys/devices/system/cpu/cpufreq/policy2/scaling_max", kin::WriteStatus::Unchanged);
    t.endBatch();
    if (t.batchNodeSummary() != "total=3,written=2,unchanged=1,failed=0") {
        std::printf("FAIL node summary: %s\n", t.batchNodeSummary().c_str()); return 7;
    }
    if (!t.batchFailedNodes().empty()) return 8;
    if (t.batch() != kin::WriteStatus::Written) return 9;

    // 失败节点路径保留给诊断, 且不可解析字符被过滤。
    t.startBatch();
    t.recordNode("/dev/cpuset/top-app/cpus", kin::WriteStatus::Failed);
    t.recordNode("/dev/cpuctl/foreground/cpu.uclamp.max\nowner=fake", kin::WriteStatus::Failed);
    t.endBatch();
    const auto summary = t.batchNodeSummary();
    if (summary != "total=2,written=0,unchanged=0,failed=2") {
        std::printf("FAIL failed summary: %s\n", summary.c_str()); return 10;
    }
    const auto failed = t.batchFailedNodes();
    if (failed != "/dev/cpuset/top-app/cpus,/dev/cpuctl/foreground/cpu.uclamp.max_owner_fake") {
        std::printf("FAIL failed nodes: %s\n", failed.c_str()); return 11;
    }
    if (t.batch() != kin::WriteStatus::Failed) return 12;

    // 换批清空上一批节点明细, 不允许上一拍的失败路径冒充本拍结果。
    t.startBatch();
    if (!t.batchNodeSummary().empty() || !t.batchFailedNodes().empty()) {
        std::puts("FAIL batch reset kept stale node records"); return 13;
    }
    t.endBatch();

    // 其他线程的节点记录只更新全局状态, 不进本线程批次。
    t.startBatch();
    std::thread other([&] {
        t.recordNode("/sys/devices/system/cpu/cpufreq/policy0/scaling_max", kin::WriteStatus::Failed);
    });
    other.join();
    t.endBatch();
    if (!t.batchNodeSummary().empty() || t.batch() != kin::WriteStatus::Unknown) {
        std::puts("FAIL unrelated worker contaminated node batch"); return 14;
    }

    // 失败路径上限: 超出部分计入 dropped, 但计数仍然完整。
    t.startBatch();
    for (int i = 0; i < 7; ++i) t.recordNode("/dev/cpuset/cpus", kin::WriteStatus::Failed);
    t.endBatch();
    if (t.batchNodes().failed != 7 || t.batchNodes().dropped != 3) {
        std::printf("FAIL dropped count: failed=%u dropped=%u\n",
                    t.batchNodes().failed, t.batchNodes().dropped); return 15;
    }
    if (t.batchNodeSummary().find(",dropped=3") == std::string::npos) return 16;
    if (t.batchFailedNodes() != "/dev/cpuset/cpus,/dev/cpuset/cpus,/dev/cpuset/cpus,/dev/cpuset/cpus") {
        return 17;
    }

    // 超长路径截断到 kMaxNodePath-1, 不越界。
    std::string longPath = "/sys/";
    while (longPath.size() < 300) longPath += "abcdefghij";
    t.startBatch();
    t.recordNode(longPath.c_str(), kin::WriteStatus::Failed);
    t.endBatch();
    if (t.batchFailedNodes().size() != kin::WriteStatusTracker::kMaxNodePath - 1) {
        std::printf("FAIL truncation: %zu\n", t.batchFailedNodes().size()); return 18;
    }

    std::puts("PASS write status: failure dominance, empty batch, thread isolation, per-node detail");
}
