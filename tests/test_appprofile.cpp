#include "Config.hpp"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using namespace Config::AppProfile;
    enable = true;
    modelCount = 2;
    Models[0].modelName = "game_heavy";
    Models[0].isGame = true;
    Models[0].packages[0] = "com.tencent.tmgp.*";
    Models[0].packageCount = 1;
    Models[1].modelName = "powersave";
    perAppPkg[0] = "com.tencent.mm";
    perAppModel[0] = "powersave";
    perAppCount = 1;
    packageBlacklist[0] = "com.android.launcher*";
    blacklistCount = 1;

    auto game = decideForeground("com.tencent.tmgp.sgame", false);
    CHECK(game.matched == 0 && game.selected == 0 && game.applyProfile && !game.release);
    auto social = decideForeground("com.tencent.mm", false);
    CHECK(social.selected == 1 && social.applyProfile);
    auto launcher = decideForeground("com.android.launcher3", false);
    CHECK(launcher.release && !launcher.applyProfile && launcher.selected == -1);
    auto unknown = decideForeground("com.unknown.app", false);
    CHECK(!unknown.release && !unknown.applyProfile && unknown.selected == -1);
    perAppGlobal = "powersave";
    CHECK(decideForeground("com.unknown.app", false).selected == 1);
    CHECK(decideForeground("com.android.launcher3", false).release);
    CHECK(decideForeground("com.tencent.tmgp.sgame", false).selected == 0);
    perAppGlobal = "missing_model";
    CHECK(decideForeground("com.unknown.app", false).selected == -1);
    perAppGlobal = "powersave";
    auto automatic = decideForeground("com.tencent.tmgp.sgame", true);
    CHECK(automatic.selected == -1 && !automatic.applyProfile && !automatic.release);
    CHECK(!decideForeground("com.android.launcher3", true).release);
    enable = false;
    CHECK(!decideForeground("com.tencent.mm", false).applyProfile);
    CHECK(decideForeground(nullptr, false).selected == -1);
    std::puts("PASS: production foreground decision, blacklist, global fallback and auto handoff");
}
