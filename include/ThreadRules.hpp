#pragma once
// ── AppOpt 式线程绑核规则引擎(纯计算) ─────────────────────────────────────────
// 解析 "进程名{线程名}=核范围" 规则, 按进程名(cmdline basename)与线程名(comm)匹配, 算出
// 目标 cpu_set_t。语义对齐 SutoLiu 大佬的 AppOpt(GPL3.0, https://gitee.com/sutoliu/AppOpt):
//   · 一行一条规则; '#' 起始或行内 '#' 之后为注释; 'USER_' 起始行(App WebUI 全局变量约定)跳过。
//   · 进程名支持通配符 (* ? [..]); 线程名支持优先级排序: 精确 > [..] > ? > * > 进程默认(空线程名)。
//   · 进程默认规则(无 '{}' 的 "pkg=cores")作为该进程未命中具体线程规则时的兜底。
// 本类只做「解析 + 匹配」, 不碰任何系统节点; /proc 扫描与 sched_setaffinity 由 scheduler 负责。
// 规则文本与 App 侧 ThreadAllocRules(thread_alloc_rules.conf)、AppOpt applist.conf 完全兼容。
#include <sched.h>
#include <fnmatch.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <cctype>

class ThreadRules {
public:
    struct Rule {
        std::string pkg;      // 进程名(cmdline basename); 可含通配 * ? [
        std::string thread;   // 线程名(comm)模式; 空串 = 该进程默认兜底
        cpu_set_t   cpus;     // 目标核集合(已按 present 过滤)
        bool        pkgWildcard;
        int         priority; // 线程名优先级: 精确 1000+len, [..] 500+, ? 300+, * 100+, 空 200
    };

    // 解析规则文本, 重建索引。present 为本机在线核集合(过滤越界/离线核), 传 nullptr 则不过滤。
    // 返回加载成功的规则条数。任何单行错误只跳过该行, 不影响其余规则。
    size_t load(const std::string& text, const cpu_set_t* present) {
        clear();
        size_t start = 0;
        const size_t n = text.size();
        while (start <= n) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? n : nl;
            parseLine(text.data() + start, end - start, present);
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

    // 不依赖 glibc _GNU_SOURCE 门控的 CPU_COUNT/CPU_EQUAL 替代(bionic 亦无需), 供 scheduler 复用。
    static int cpuCount(const cpu_set_t* s) {
        int n = 0;
        for (int i = 0; i < CPU_SETSIZE; i++) if (CPU_ISSET(i, s)) n++;
        return n;
    }
    static bool cpuEqual(const cpu_set_t* a, const cpu_set_t* b) {
        for (int i = 0; i < CPU_SETSIZE; i++)
            if (CPU_ISSET(i, a) != CPU_ISSET(i, b)) return false;
        return true;
    }

    // 进程名是否命中任一规则(精确包名 或 通配包名)。命中才值得进一步扫它的线程。
    bool matchesProcess(const char* procName) const {
        if (!procName || !*procName) return false;
        if (exactByPkg_.find(procName) != exactByPkg_.end()) return true;
        for (int idx : wildcardIdx_)
            if (fnmatch(rules_[idx].pkg.c_str(), procName, 0) == 0) return true;
        return false;
    }

    // 收集某进程名命中的全部规则(精确包 + 通配包), 供逐线程解析复用。返回是否命中 >=1 条。
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

    // 在已收集的进程规则集里解析某线程名 → 目标核集合。
    // 优先级: 精确线程名(strcmp) > 通配线程名(按 priority 取最高) > 进程默认(空线程名, 取包名最具体者)。
    // 返回 false = 该线程既无具体规则也无进程默认 → 不应绑核(保持原样)。
    static bool resolveThread(const std::vector<const Rule*>& procRules,
                              const char* threadName, cpu_set_t* out) {
        const Rule* def = nullptr;      // 进程默认(空线程名)中包名最具体者(精确包 > 通配包)
        int defPrio = -1;
        const Rule* best = nullptr;     // 命中的最高优先级通配线程规则
        int bestPrio = -1;
        const char* tn = threadName ? threadName : "";
        for (const Rule* r : procRules) {
            if (r->thread.empty()) {
                // 多条默认规则命中同一进程(如精确包 com.foo 与通配包 com.*)时, 取优先级更高者
                // (priority 对空线程名规则 = calcPriority(包名), 精确包 1000+len 胜通配包 100+), 对齐 AppOpt。
                if (r->priority > defPrio) { defPrio = r->priority; def = r; }
                continue;
            }
            if (std::strcmp(r->thread.c_str(), tn) == 0) {   // 精确线程名 → 立即命中
                if (out) *out = r->cpus;
                return true;
            }
            if (r->priority < 1000 && fnmatch(r->thread.c_str(), tn, 0) == 0) {
                if (r->priority > bestPrio) { bestPrio = r->priority; best = r; }
            }
        }
        if (best) { if (out) *out = best->cpus; return true; }
        if (def)  { if (out) *out = def->cpus;  return true; }
        return false;
    }

private:
    static constexpr size_t kMaxPkgLen    = 128;
    static constexpr size_t kMaxThreadLen = 32;

    std::vector<Rule> rules_;
    // 精确包名 → 规则下标列表; 通配包名规则下标单列。加速 matchesProcess / collectForProcess。
    std::unordered_map<std::string, std::vector<int>> exactByPkg_;
    std::vector<int> wildcardIdx_;

    static bool isWildcard(const std::string& s) {
        return s.find('*') != std::string::npos ||
               s.find('?') != std::string::npos ||
               s.find('[') != std::string::npos;
    }

    // 线程名模式优先级(与 AppOpt calculate_rule_priority 一致):
    // 空 → 200; 无通配符(精确) → 1000+len; 含 '[' → 500+非通配字符数; 含 '?' → 300+; 仅 '*' → 100+。
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

    // 解析核范围串("7"、"4-6"、"0-2,7") → cpu_set_t。present!=null 时与在线核取交集
    // (越界/离线核跳过, 而非作废整条 —— App 侧只按 0..127 校验, 这里宽松取交避免整条规则被静默丢弃)。
    // 返回 false = 语法非法(a>b/负数) 或 交集为空。
    static bool parseCpuRanges(const std::string& spec, cpu_set_t* set, const cpu_set_t* present) {
        CPU_ZERO(set);
        const char* s = spec.c_str();
        bool any = false;
        while (*s) {
            char* end = nullptr;
            long a = std::strtol(s, &end, 10);
            if (end == s) { s++; continue; }       // 跳过分隔符/杂字符
            long b = a;
            if (*end == '-') {
                const char* s2 = end + 1;
                b = std::strtol(s2, &end, 10);
                if (end == s2) b = a;
            }
            if (a < 0 || b < a) return false;      // 语法非法 → 整条作废
            for (long i = a; i <= b && i < CPU_SETSIZE; i++) {
                if (present && !CPU_ISSET(i, present)) continue;   // 越界/离线核: 跳过, 保留有效核
                CPU_SET(i, set);
                any = true;
            }
            s = (*end == ',') ? end + 1 : end;
        }
        return any;
    }

    // 解析单行("pkg{thread}=cores" 或 "pkg=cores"); 合法则追加进 rules_。
    void parseLine(const char* p, size_t len, const cpu_set_t* present) {
        // 去首尾空白
        size_t b = 0, e = len;
        while (b < e && std::isspace((unsigned char)p[b])) b++;
        while (e > b && std::isspace((unsigned char)p[e - 1])) e--;
        if (b >= e) return;
        if (p[b] == '#') return;                                   // 整行注释
        std::string line(p + b, e - b);
        if (line.compare(0, 5, "USER_") == 0) return;              // App/WebUI 全局变量行

        // 行内 '#' 之后视为注释(与 AppOpt 一致)
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
            if (key.back() != '}') return;                         // 缺闭合 '}'
            pkg = trim(key.substr(0, br));
            thread = trim(key.substr(br + 1, key.size() - br - 2));
            if (thread.empty()) return;                            // "pkg{}" 非法
        } else {
            if (key.find('}') != std::string::npos) return;        // 有 '}' 无 '{'
            pkg = key;
        }
        if (pkg.empty() || pkg.size() >= kMaxPkgLen || thread.size() >= kMaxThreadLen) return;

        Rule r;
        r.pkg = pkg;
        r.thread = thread;
        if (!parseCpuRanges(val, &r.cpus, present)) return;
        r.pkgWildcard = isWildcard(pkg);
        // 与 AppOpt calculate_rule_priority(thread[0]?thread:pkg) 一致: 有线程名按线程名算优先级,
        // 空线程名(进程默认)按包名算 —— 这样多条默认命中同一进程时更具体的包胜出。
        r.priority = calcPriority(thread.empty() ? pkg : thread);

        // 去重: 同 (pkg, thread) 只保留首条(与 AppOpt 一致)
        for (const Rule& x : rules_)
            if (x.pkg == pkg && x.thread == thread) return;
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
