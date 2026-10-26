// AC-3: 以 '.' 结尾的规则是字面前缀; '*'/'?' 与精确匹配保持原样。
// 游戏库命中但没有自己的 perapp/config 规则时不合成 game_lite。
#include "Config.hpp"
#include "GameList.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

#ifndef KIN_SOURCE_DIR
#define KIN_SOURCE_DIR "."
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int checkShippedRules() {
    namespace fs = std::filesystem;
    const fs::path root(KIN_SOURCE_DIR);
    const fs::path trees[] = {root / "Json", root / "magisk" / "config"};
    int jsonCount = 0;
    int perappCount = 0;
    for (const fs::path& dir : trees) {
        CHECK(fs::is_directory(dir));
        for (const fs::directory_entry& ent : fs::directory_iterator(dir)) {
            const fs::path path = ent.path();
            const std::string name = path.filename().string();
            if (name.rfind("config", 0) == 0 && path.extension() == ".json") {
                const std::string body = readFile(path);
                CHECK(!body.empty());
                CHECK(body.find("com.tencent.tmgp.") == std::string::npos);
                ++jsonCount;
            }
        }
        const fs::path perapp = dir / "perapp_powermode.txt";
        const std::string body = readFile(perapp);
        CHECK(!body.empty());
        int wildcard = 0;
        size_t line = 0;
        while (line < body.size()) {
            size_t end = body.find('\n', line);
            if (end == std::string::npos) end = body.size();
            std::string row = body.substr(line, end - line);
            if (!row.empty() && row.back() == '\r') row.pop_back();
            if (!row.empty() && row[0] != '#' && row.rfind("com.tencent.tmgp.", 0) == 0)
                ++wildcard;
            if (end == body.size()) break;
            line = end + 1;
        }
        CHECK(wildcard == 0);
        ++perappCount;
    }
    CHECK(jsonCount >= 20);
    CHECK(perappCount == 2);
    return 0;
}

}  // namespace

int main() {
    using namespace Config::AppProfile;
    CHECK(!matchPackage(nullptr, "a"));
    CHECK(!matchPackage("a", nullptr));

    CHECK(matchPackage("prefix.", "prefix.game"));
    CHECK(matchPackage("prefix.", "prefix."));
    CHECK(!matchPackage("prefix.", "prefix"));
    CHECK(!matchPackage("prefix.", "prefixXgame"));
    CHECK(matchPackage("pre*fix.", "pre*fix.abc"));
    CHECK(!matchPackage("pre*fix.", "prefix.abc"));

    CHECK(matchPackage("prefix.*", "prefix.game"));
    CHECK(matchPackage("prefix.*", "prefix."));
    CHECK(!matchPackage("prefix.*", "prefixXgame"));
    CHECK(!matchPackage("prefix.*", "prefi.game"));
    CHECK(matchPackage("com.tencent.tmgp.*", "com.tencent.tmgp.sgame"));
    CHECK(!matchPackage("com.tencent.tmgp.*", "com.tencent.tmgpx"));
    CHECK(matchPackage("com.tencent.tmgp.", "com.tencent.tmgp.sgame"));
    CHECK(!matchPackage("com.tencent.tmgp.", "com.tencent.tmgpx"));
    CHECK(!matchPackage("com.tencent.tmgp.", "com.tencent.tmgp"));

    CHECK(matchPackage("com.foo.exact", "com.foo.exact"));
    CHECK(!matchPackage("com.foo.exact", "com.foo.exact.extra"));
    CHECK(!matchPackage("com.foo.exact", "com.foo.exac"));
    CHECK(matchPackage("launcher?", "launcher3"));
    CHECK(!matchPackage("launcher?", "launcher33"));
    CHECK(matchPackage("*", "anything"));

    modelCount = 3;
    Models[0].modelName = "game_heavy";
    Models[0].isGame = true;
    Models[0].packageCount = 1;
    Models[0].packages[0] = "com.other.heavy";
    Models[1].modelName = "game_lite";
    Models[1].isGame = true;
    Models[1].packageCount = 1;
    Models[1].packages[0] = "com.other.lite";
    Models[2].modelName = "balance";
    Models[2].isGame = false;
    Models[2].packageCount = 0;
    perAppCount = 2;
    perAppPkg[0] = "com.tencent.tmgp.*";
    perAppModel[0] = "game_heavy";
    perAppFps[0] = 0;
    perAppPkg[1] = "com.foo.exact";
    perAppModel[1] = "game_lite";
    perAppFps[1] = 0;
    perAppGlobal = "balance";
    blacklistCount = 0;

    const char* listed = "com.TeamCherry.HollowKnight";
    CHECK(Config::GameList::contains(listed));
    const int explicitListed = profileIfExplicit(findMatchingModel(listed, false));
    CHECK(explicitListed == -1);
    const int globalListed = findMatchingModel(listed, true);
    CHECK(globalListed == 2);
    CHECK(Models[globalListed].modelName == "balance");
    CHECK(!(Models[globalListed].modelName == "game_lite"));

    const int sgame = profileIfExplicit(findMatchingModel("com.tencent.tmgp.sgame", false));
    CHECK(sgame == 0);
    CHECK(Models[sgame].modelName == "game_heavy");
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgpx", false)) == -1);
    CHECK(findMatchingModel("com.tencent.tmgpx", true) == 2);
    CHECK(profileIfExplicit(findMatchingModel("com.foo.exact", false)) == 1);

    perAppCount = 3;
    perAppPkg[2] = "com.tencent.tmgp.";
    perAppModel[2] = "game_heavy";
    perAppFps[2] = 0;
    // perapp 先匹配 com.tencent.tmgp.* , 前缀规则排在后面也不会把 tmgpx 收进来。
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgp.sgame", false)) == 0);
    perAppPkg[0] = "com.tencent.tmgp.";
    perAppModel[0] = "game_heavy";
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgp.sgame", false)) == 0);
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgpx", false)) == -1);

    // AC-3b: 没有自己的规则时走全局基础档; 用户显式写成 game_heavy 才接管。
    modelCount = 2;
    Models[0].modelName = "game_heavy";
    Models[0].isGame = true;
    Models[0].packageCount = 0;
    Models[1].modelName = "balance";
    Models[1].isGame = false;
    Models[1].packageCount = 0;
    perAppCount = 0;
    perAppGlobal = "balance";
    blacklistCount = 0;
    const char* unsetTmgp[] = {
        "com.tencent.tmgp.sgame",
        "com.tencent.tmgp.jjds",
        "com.tencent.tmgp.qz",
        "com.tencent.tmgp.carrot3dj",
        "com.tencent.tmgp.gnyx",
    };
    for (const char* pkg : unsetTmgp) {
        CHECK(profileIfExplicit(findMatchingModel(pkg, false)) == -1);
        const int base = findMatchingModel(pkg, true);
        CHECK(base == 1);
        CHECK(Models[base].modelName == "balance");
    }
    perAppCount = 1;
    perAppPkg[0] = "com.tencent.tmgp.sgame";
    perAppModel[0] = "game_heavy";
    perAppFps[0] = 0;
    const int explicitHeavy = profileIfExplicit(findMatchingModel("com.tencent.tmgp.sgame", false));
    CHECK(explicitHeavy == 0);
    CHECK(Models[explicitHeavy].modelName == "game_heavy");
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgp.jjds", false)) == -1);
    CHECK(findMatchingModel("com.tencent.tmgp.gnyx", true) == 1);
    perAppCount = 0;
    Models[0].packageCount = 1;
    Models[0].packages[0] = "com.tencent.tmgp.gnyx";
    const int fromConfig = profileIfExplicit(findMatchingModel("com.tencent.tmgp.gnyx", false));
    CHECK(fromConfig == 0);
    CHECK(Models[fromConfig].modelName == "game_heavy");
    CHECK(profileIfExplicit(findMatchingModel("com.tencent.tmgp.qz", false)) == -1);
    CHECK(findMatchingModel("com.tencent.tmgp.carrot3dj", true) == 1);

    CHECK(checkShippedRules() == 0);
    std::puts("PASS: prefix/glob/exact match, unset tmgp follows base, explicit game_heavy takes over");
    return 0;
}
