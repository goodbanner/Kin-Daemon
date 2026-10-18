#pragma once
#include "PolicyResolver.hpp"
#include "WriteStatus.hpp"
#include <string_view>

namespace kin {
inline std::string_view policyStatusToken(std::string_view token) {
    if (token.empty() || token.size() > 64 ||
        token.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != token.npos)
        return "unknown";
    return token;
}

// These are decision diagnostics, not sysfs acknowledgements or a global GPU policy.
inline std::string policyStatusFields(const PolicyDecision& decision, bool fasEngaged,
                                      bool romManaged, WriteStatus writeStatus = WriteStatus::Unknown,
                                      std::string_view writeScope = "fsm_cpu_batch",
                                      std::string_view writeNodes = {},
                                      std::string_view writeFailedNodes = {}) {
    std::string out = "resolved_policy=";
    out += romManaged ? "rom_managed" : policyStatusToken(decision.policy);
    out += "\npolicy_reason=";
    out += romManaged ? "fengchi_takeover" : policyStatusToken(decision.reason);
    out += "\npolicy_scope=cpu\nfas_state=";
    out += romManaged ? "blocked" : fasEngaged ? "engaged" : "inactive";
    out += "\nwrite_verification=daemon_write_result\nlast_write_scope=";
    out += policyStatusToken(writeScope);
    out += "\nlast_write_result=";
    out += writeStatusName(romManaged ? WriteStatus::Unknown : writeStatus);
    out += "\n";
    // 逐节点明细由 tracker 预先做安全字符过滤; 风驰接管时整批未执行, 不输出免得误导。
    if (!romManaged && !writeNodes.empty()) {
        out += "last_write_nodes=";
        out += writeNodes;
        out += "\n";
        if (!writeFailedNodes.empty()) {
            out += "last_write_failed=";
            out += writeFailedNodes;
            out += "\n";
        }
    }
    return out;
}
} // namespace kin
