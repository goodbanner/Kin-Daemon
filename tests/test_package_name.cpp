#include "PackageName.hpp"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    for (const char* pkg : {"android", "com.tencent.mm", "com.example.App_123"})
        CHECK(kin::validPackageName(pkg));
    int calls = 0;
    std::string command;
    auto read = [&](const char* cmd) { ++calls; command = cmd; };
    for (const char* pkg : {"", ".com", "com.", "com..app", "com.1app", "-h",
                           "com.app;id", "com.app$(id)", "com.app`id`", "com.app|id",
                           "com.app\nwhoami", "com.app name", "com.app'", "com.app:service"}) {
        CHECK(!kin::validPackageName(pkg));
        CHECK(!kin::readPackageCategory(pkg, read));
    }
    CHECK(!kin::readPackageCategory(std::string(256, 'a'), read));
    CHECK(!kin::readPackageCategory(std::string("com.app\0;id", 11), read));
    CHECK(calls == 0);
    CHECK(kin::readPackageCategory("com.example.app", read));
    CHECK(calls == 1 && command.find("package 'com.example.app'") != std::string::npos);
    CHECK(kin::readPackageCategory(std::string(255, 'a'), read));
    CHECK(command.find(std::string(255, 'a')) != std::string::npos);
    std::puts("PASS: package boundary rejects shell syntax without executing commands");
}
