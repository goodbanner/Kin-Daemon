#include "PolicyStatus.hpp"
#include <cstdio>

int main() {
    kin::PolicyDecision decision{{300, 1000, 400, 1200}, "thermal", "joint_dvfs"};
    const auto normal = kin::policyStatusFields(decision, true, false, kin::WriteStatus::Unknown);
    if (normal != "resolved_policy=thermal\npolicy_reason=joint_dvfs\n"
                  "policy_scope=cpu\nfas_state=engaged\nwrite_verification=daemon_write_result\n"
                  "last_write_scope=fsm_cpu_batch\nlast_write_result=unknown\n") return 1;
    const auto custom = kin::policyStatusFields(decision, false, false, kin::WriteStatus::Written, "sysfs_cpu");
    if (custom.find("last_write_scope=sysfs_cpu\n") == std::string::npos) return 2;
    const auto invalidScope = kin::policyStatusFields(decision, false, false, kin::WriteStatus::Written, "bad scope");
    if (invalidScope.find("last_write_scope=unknown\n") == std::string::npos) return 2;
    const auto rom = kin::policyStatusFields(decision, false, true);
    if (rom.find("resolved_policy=rom_managed\npolicy_reason=fengchi_takeover\n") != 0 ||
        rom.find("fas_state=blocked\n") == std::string::npos) return 2;
    decision.policy = "game\nowner=fake";
    decision.reason = "bad\r\nkey=value";
    const auto invalid = kin::policyStatusFields(decision, false, false);
    if (invalid.find("resolved_policy=unknown\npolicy_reason=unknown\n") != 0 ||
        invalid.find("fas_state=inactive\n") == std::string::npos) return 3;
    for (auto status : {kin::WriteStatus::Written, kin::WriteStatus::Failed, kin::WriteStatus::Unchanged}) {
        const auto fields = kin::policyStatusFields(decision, false, false, status);
        if (fields.find(std::string("last_write_result=") + kin::writeStatusName(status) + "\n") == std::string::npos ||
            fields.find("write_verification=daemon_write_result\n") == std::string::npos) return 4;
        const auto taken = kin::policyStatusFields(decision, false, true, status);
        if (taken.find("last_write_result=unknown\n") == std::string::npos) return 5;
    }
    // 逐节点明细: 默认不输出(保持旧输出字节不变), 有明细时追加两个诊断键。
    if (normal.find("last_write_nodes") != std::string::npos) return 6;
    const auto detailed = kin::policyStatusFields(decision, false, false, kin::WriteStatus::Failed,
                                                  "fsm_cpu_batch", "total=3,written=2,unchanged=0,failed=1",
                                                  "/dev/cpuset/top-app/cpus");
    if (detailed.find("\nlast_write_nodes=total=3,written=2,unchanged=0,failed=1\n") == std::string::npos ||
        detailed.find("\nlast_write_failed=/dev/cpuset/top-app/cpus\n") == std::string::npos) return 7;
    const auto cleanBatch = kin::policyStatusFields(decision, false, false, kin::WriteStatus::Unchanged,
                                                    "fsm_cpu_batch", "total=2,written=0,unchanged=2,failed=0");
    if (cleanBatch.find("last_write_nodes=total=2") == std::string::npos ||
        cleanBatch.find("last_write_failed") != std::string::npos) return 8;
    // 风驰接管时整批未执行, 不得附带节点明细。
    const auto takenDetailed = kin::policyStatusFields(decision, false, true, kin::WriteStatus::Written,
                                                       "fsm_cpu_batch", "total=1,written=1,unchanged=0,failed=0",
                                                       "/dev/cpuset/cpus");
    if (takenDetailed.find("last_write_nodes") != std::string::npos) return 9;
    std::puts("PASS policy status: CPU scope, ROM takeover, write result, per-node detail, safe tokens");
}
