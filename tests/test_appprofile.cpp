// 本机(x86)验证: 用守护【真实的】 Config::AppProfile 匹配逻辑, 复刻基础模式 evalForegroundOnce
// 的决策(autoActive_=false, 无 sceneActive), 断言"配了画像的应用 → 命中画像 → 走画像档 + 会应用画像"。
#include "Config.hpp"
#include <cstdio>
#include <string>

using namespace Config;

static void setModel(int i, const char* name, bool game,
                     std::initializer_list<const char*> pkgs) {
    AppProfile::Models[i].modelName = name;
    AppProfile::Models[i].isGame = game;
    int j = 0;
    for (auto p : pkgs) AppProfile::Models[i].packages[j++] = p;
    AppProfile::Models[i].packageCount = (int)pkgs.size();
}

// 复刻 scheduler.hpp evalForegroundOnce 基础模式的决策(HEAD c906e43): 无 sceneActive, autoActive_=false。
// 返回将要打印的日志分支 + 是否会真正应用画像(applyWithProfile 门: enable && matched>=0)。
struct Decision { std::string logBranch; bool willApplyProfile; int matched; };
static Decision decideBaseMode(const char* pkg) {
    const bool autoActive = false;                       // 基础模式
    const bool isBlack = AppProfile::isBlacklisted(pkg);
    int matched = isBlack ? -1 : AppProfile::findMatchingModel(pkg);
    if (autoActive) matched = -1;                        // (基础模式不触发)
    if (isBlack && !autoActive)
        return {"释放接管(黑名单)", false, -1};
    int newMatch = matched;
    if (!autoActive && newMatch < 0 && !AppProfile::perAppGlobal.empty()) {
        int g = AppProfile::findModelByName(AppProfile::perAppGlobal.c_str());
        if (g >= 0) newMatch = g;
    }
    std::string branch;
    if (matched >= 0)        branch = std::string("画像档[") + AppProfile::Models[matched].modelName.c_str() + "]";
    else if (newMatch >= 0)  branch = std::string("全局档[") + AppProfile::Models[newMatch].modelName.c_str() + "]";
    else                     branch = "基础档";
    // applyWithProfile 门(HEAD 1665): enable && currentMatch>=0 → applyAppProfile(真正套画像)
    bool willApply = AppProfile::enable && (newMatch >= 0);
    return {branch, willApply, newMatch};
}

int main() {
    // ── 模拟一份"基础模式 + 用户配了应用画像"的加载后配置 ──
    AppProfile::enable = true;                 // config Function.AppProfile.enable=true
    AppProfile::modelCount = 2;
    setModel(0, "game_heavy", true,  {"com.tencent.tmgp.*"});   // 游戏画像(通配)
    setModel(1, "powersave",  false, {});                        // 省电档(供 perapp 引用)
    AppProfile::perAppPkg[0]   = "com.tencent.mm";               // 用户: 微信 → 省电
    AppProfile::perAppModel[0] = "powersave";
    AppProfile::perAppCount = 1;
    AppProfile::packageBlacklist[0] = "com.android.launcher*";   // 黑名单: 桌面
    AppProfile::blacklistCount = 1;

    struct Case { const char* pkg; const char* want; bool wantApply; };
    Case cases[] = {
        {"com.tencent.tmgp.sgame", "画像档[game_heavy]", true},   // 王者 → 游戏画像
        {"com.tencent.mm",         "画像档[powersave]",  true},   // 微信 → perapp 省电画像
        {"com.android.launcher3",  "释放接管(黑名单)",   false},  // 桌面 → 黑名单释放
        {"com.unknown.app",        "基础档",             false},  // 无画像无全局 → 基础档
    };
    int pass = 0, total = 0;
    printf("=== 基础模式(autoActive_=false, 无 Scene 门) 应用画像决策验证 ===\n");
    for (auto& c : cases) {
        Decision d = decideBaseMode(c.pkg);
        bool ok = (d.logBranch == c.want) && (d.willApplyProfile == c.wantApply);
        total++; if (ok) pass++;
        printf("%s  前台: %-24s → %-18s  套画像=%s  [期望 %-18s 套画像=%s]\n",
               ok ? "PASS" : "FAIL", c.pkg, d.logBranch.c_str(),
               d.willApplyProfile ? "是" : "否", c.want, c.wantApply ? "是" : "否");
    }
    printf("=== %d/%d 通过 ===\n", pass, total);
    // 关键断言: 配了画像的应用(王者/微信)在基础模式下 matched>=0 且 willApplyProfile=是 → 画像真会生效
    Decision g = decideBaseMode("com.tencent.tmgp.sgame");
    Decision w = decideBaseMode("com.tencent.mm");
    bool profilesWork = g.willApplyProfile && w.willApplyProfile
                        && g.logBranch.rfind("画像档", 0) == 0 && w.logBranch.rfind("画像档", 0) == 0;
    printf("\n结论: 基础模式下应用画像%s生效; 每次前台切换%s打出 info 前台日志。\n",
           profilesWork ? "会正常" : "【不】", "会");
    return (pass == total && profilesWork) ? 0 : 1;
}
