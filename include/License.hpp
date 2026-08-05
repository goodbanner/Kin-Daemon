#pragma once
// 授权状态(App 驱动):
//   付费判定完全交给 App —— App 用卡密/订单在自己那边联网校验(见 App 的 license/License.kt),
//   确认付费后把「已付费」这一状态写给守护(auth.json)。守护【只信任 App 的信号】,
//   不再自行读取/验签 license.json、不再做设备绑定/令牌到期,更不会因为"没授权"就自杀。
//   守护无条件运行; auth.json 只用来让守护「知道」当前是否已付费(记录 + 供高级行为按需门控)。
//   —— 这样卡密体系收敛到 App 一处(功能级付费墙), 守护永远可用, 不会把用户(含开发者自己)锁死。
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <ctime>

namespace lic {

using std::size_t;

// ---------------- 简单 JSON 扁平字段提取 ----------------
// 取 "name":"value"(字符串)或 "name":true/数字。找不到返回空串。
// 精确匹配键名:要求 "name" 后紧跟冒号(允许空白),避免 "paid" 误匹配 "paid_at" 之类。
inline std::string jsonStr(const std::string& j, const char* name) {
    std::string pat = std::string("\"") + name + "\"";
    size_t p = 0;
    while ((p = j.find(pat, p)) != std::string::npos) {
        size_t after = p + pat.size();
        size_t k = after;
        while (k < j.size() && (j[k]==' '||j[k]=='\t'||j[k]=='\n'||j[k]=='\r')) k++;
        if (k < j.size() && (j[k]==':' || j[k]==',' || j[k]=='}')) {
            p = j.find(':', k); if (p == std::string::npos) return "";
            p++;
            while (p < j.size() && (j[p]==' '||j[p]=='\t'||j[p]=='\n'||j[p]=='\r')) p++;
            if (p >= j.size()) return "";
            if (j[p] == '"') {
                p++; std::string out;
                while (p < j.size() && j[p] != '"') { if (j[p]=='\\' && p+1<j.size()) p++; out += j[p++]; }
                return out;
            }
            size_t e = p; while (e < j.size() && j[e]!=','&&j[e]!='}'&&j[e]!=' '&&j[e]!='\n'&&j[e]!='\r') e++;
            std::string v = j.substr(p, e-p);
            if (v == "null") return "";
            return v;
        }
        p = after;
    }
    return "";
}

// 读文件全部内容
inline std::string readAll(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return "";
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b,1,sizeof b,f)) > 0) s.append(b, n);
    fclose(f); return s;
}

// 当前 UTC,ISO8601(与 App 写入的 expires_at 同格式,可字典序比较)
inline std::string nowIsoUtc() {
    time_t t = time(nullptr); struct tm g; gmtime_r(&t, &g);
    char b[32]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S.000Z", &g);
    return b;
}

// ok  : 守护是否放行运行 —— 恒为 true(授权不再阻断守护)。
// paid: App 下发的付费状态 —— 供守护「知道」是否已付费(记录/按需门控高级行为)。
struct Result { bool ok; bool paid; std::string reason; std::string plan; std::string expiresAt; };

// 读取 App 下发的授权信号 auth.json。App 是付费权威,守护只信任该信号。
//   格式: { "paid": true, "plan": "forever", "expires_at": "" }
//   读不到 / 未付费 → paid=false, 但 ok 仍为 true(守护照常运行, 绝不阻断)。
inline Result readAuth(const char* path = "/sdcard/Android/Kin/AI/auth.json") {
#ifdef KIN_NO_LICENSE
    // 免授权构建:恒视为已付费。
    (void)path;
    return {true, true, "ok(no_license_build)", "", ""};
#else
    std::string j = readAll(path);
    if (j.empty()) return {true, false, "未收到 App 授权信号(未付费, 或 App 尚未运行)", "", ""};
    std::string paid = jsonStr(j, "paid");
    std::string plan = jsonStr(j, "plan");
    std::string exp  = jsonStr(j, "expires_at");   // 订阅到期(永久为空)
    bool isPaid = (paid == "true" || paid == "1");
    // 订阅到期(可选): 有 expires_at 且已过 UTC 现在 → 视为未付费
    if (isPaid && !exp.empty() && exp < nowIsoUtc()) {
        return {true, false, "订阅已过期", plan, exp};
    }
    return {true, isPaid, isPaid ? "ok" : "未付费", plan, exp};
#endif
}

} // namespace lic
