#pragma once
// ── 实时调度优先级规则引擎(纯计算) ───────────────────────────────────────────
// 解析 "进程名{线程名}=策略 优先级" 规则(与 SutoLiu 的 AppOpt priority.prop 兼容):
//   策略 ∈ SCHED_OTHER(标准分时, 优先级必须 0) / SCHED_FIFO(实时先进先出, 1-99) /
//          SCHED_RR(实时轮转, 1-99)。
// 匹配语义与 ThreadRules 一致: 精确线程名 > [..] > ? > * > 进程默认(空线程名)。
// 本类只做「解析 + 匹配」, 不碰任何系统调用; sched_setscheduler 与 /proc 扫描由 scheduler 负责。
#include <sched.h>
#include <fnmatch.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <cctype>

class ThreadPriorityRules {
public:
    struct PrioVal { int policy; int prio; };   // policy = SCHED_*; prio = sched_priority

    struct Rule {
        std::string pkg;
        std::string thread;       // 空串 = 该进程默认兜底
        int         policy;       // SCHED_OTHER / SCHED_FIFO / SCHED_RR
        int         prio;         // 0(OTHER) 或 1..99(FIFO/RR)
        bool        pkgWildcard;
        int         matchPriority; // 线程名匹配优先级(择优用): 精确 1000+len, [..] 500+, ? 300+, * 100+, 空 200
    };

    // 解析规则文本, 重建索引。返回加载成功的规则条数。单行错误只跳过该行。
    size_t load(const std::string& text) {
        clear();
        size_t start = 0;
        const size_t n = text.size();
        while (start <= n) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? n : nl;
            parseLine(text.data() + start, end - start);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        buildIndex();
        return rules_.size();
    }

    void clear() {
        rules_.clear();
        exactByPkg_.clear();
        wildcardIdx_.clear();
    }

    bool   empty() const { return rules_.empty(); }
    size_t size()  const { return rules_.size(); }

    bool matchesProcess(const char* procName) const {
        if (!procName || !*procName) return false;
        if (exactByPkg_.find(procName) != exactByPkg_.end()) return true;
        for (int idx : wildcardIdx_)
            if (fnmatch(rules_[idx].pkg.c_str(), procName, 0) == 0) return true;
        return false;
    }

    bool collectForProcess(const char* procName, std::vector<const Rule*>& out) const {
        out.clear();
        if (!procName || !*procName) return false;
        auto it = exactByPkg_.find(procName);
        if (it != exactByPkg_.end())
            for (int idx : it->second) out.push_back(&rules_[idx]);
        for (int idx : wildcardIdx_)
            if (fnmatch(rules_[idx].pkg.c_str(), procName, 0) == 0) out.push_back(&rules_[idx]);
        return !out.empty();
    }

    // 解析某线程名 → 目标(策略, 优先级)。精确线程名 > 通配(取最高) > 进程默认(空线程名, 取包名最具体者)。
    // 返回 false = 无规则命中 → 不动该线程调度类。
    static bool resolveThread(const std::vector<const Rule*>& procRules,
                              const char* threadName, PrioVal* out) {
        const Rule* def = nullptr;
        int defPrio = -1;
        const Rule* best = nullptr;
        int bestPrio = -1;
        const char* tn = threadName ? threadName : "";
        for (const Rule* r : procRules) {
            if (r->thread.empty()) {
                if (r->matchPriority > defPrio) { defPrio = r->matchPriority; def = r; }
                continue;
            }
            if (std::strcmp(r->thread.c_str(), tn) == 0) {
                if (out) { out->policy = r->policy; out->prio = r->prio; }
                return true;
            }
            if (r->matchPriority < 1000 && fnmatch(r->thread.c_str(), tn, 0) == 0) {
                if (r->matchPriority > bestPrio) { bestPrio = r->matchPriority; best = r; }
            }
        }
        if (best) { if (out) { out->policy = best->policy; out->prio = best->prio; } return true; }
        if (def)  { if (out) { out->policy = def->policy;  out->prio = def->prio;  } return true; }
        return false;
    }

private:
    std::vector<Rule> rules_;
    std::unordered_map<std::string, std::vector<int>> exactByPkg_;
    std::vector<int> wildcardIdx_;

    static bool isWildcard(const std::string& s) {
        return s.find('*') != std::string::npos ||
               s.find('?') != std::string::npos ||
               s.find('[') != std::string::npos;
    }

    static int calcPriority(const std::string& p) {
        if (p.empty()) return 200;
        if (!isWildcard(p)) return 1000 + (int)p.size();
        int nonWild = 0;
        bool hasRange = false, hasQ = false, hasStar = false;
        for (char c : p) {
            if      (c == '[') hasRange = true;
            else if (c == '?') hasQ = true;
            else if (c == '*') hasStar = true;
            else nonWild++;
        }
        if (hasRange) return 500 + nonWild;
        if (hasQ)     return 300 + nonWild;
        if (hasStar)  return 100 + nonWild;
        return nonWild;
    }

    // 解析策略串 → SCHED_*; 未知返回 -1。
    static int parsePolicy(const std::string& s) {
        if (s == "SCHED_OTHER" || s == "OTHER" || s == "other") return SCHED_OTHER;
        if (s == "SCHED_FIFO"  || s == "FIFO"  || s == "fifo")  return SCHED_FIFO;
        if (s == "SCHED_RR"    || s == "RR"    || s == "rr")    return SCHED_RR;
        return -1;
    }

    // 解析单行("pkg{thread}=POLICY PRIO" 或 "pkg=POLICY PRIO")。
    void parseLine(const char* p, size_t len) {
        size_t b = 0, e = len;
        while (b < e && std::isspace((unsigned char)p[b])) b++;
        while (e > b && std::isspace((unsigned char)p[e - 1])) e--;
        if (b >= e) return;
        if (p[b] == '#') return;
        std::string line(p + b, e - b);
        if (line.compare(0, 5, "USER_") == 0) return;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);

        size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty() || val.empty()) return;

        std::string pkg, thread;
        size_t br = key.find('{');
        if (br != std::string::npos) {
            if (key.back() != '}') return;
            pkg = trim(key.substr(0, br));
            thread = trim(key.substr(br + 1, key.size() - br - 2));
            if (thread.empty()) return;
        } else {
            if (key.find('}') != std::string::npos) return;
            pkg = key;
        }
        if (pkg.empty() || pkg.size() >= 128 || thread.size() >= 32) return;

        // 值形如 "SCHED_FIFO 10": 策略 + 优先级(空白分隔)
        size_t sp = 0;
        while (sp < val.size() && !std::isspace((unsigned char)val[sp])) sp++;
        std::string polStr = val.substr(0, sp);
        std::string prioStr = trim(val.substr(sp));
        int policy = parsePolicy(polStr);
        if (policy < 0) return;
        int prio = prioStr.empty() ? 0 : std::atoi(prioStr.c_str());

        // 合法性: FIFO/RR 优先级必须 1..99; OTHER 必须 0。越界作废。
        if (policy == SCHED_FIFO || policy == SCHED_RR) {
            if (prio < 1 || prio > 99) return;
        } else {
            prio = 0;   // OTHER: 强制 0(容错, 不作废)
        }

        Rule r;
        r.pkg = pkg;
        r.thread = thread;
        r.policy = policy;
        r.prio = prio;
        r.pkgWildcard = isWildcard(pkg);
        r.matchPriority = calcPriority(thread);

        for (const Rule& x : rules_)
            if (x.pkg == pkg && x.thread == thread) return;   // 去重(同 pkg+thread 保留首条)
        rules_.push_back(std::move(r));
    }

    void buildIndex() {
        exactByPkg_.clear();
        wildcardIdx_.clear();
        for (int i = 0; i < (int)rules_.size(); i++) {
            if (rules_[i].pkgWildcard) wildcardIdx_.push_back(i);
            else exactByPkg_[rules_[i].pkg].push_back(i);
        }
    }

    static std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) b++;
        while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
        return s.substr(b, e - b);
    }
};
