#include "Utils.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

int main() {
    Utils utils;
    const std::string missing = "/tmp/kin_missing_sys_node_" + std::to_string((long)getpid());
    if (utils.FileWriteExisting(missing.c_str(), "1")) {
        std::fprintf(stderr, "missing node unexpectedly reported writable\n");
        return 1;
    }
    if (access(missing.c_str(), F_OK) == 0) {
        std::fprintf(stderr, "missing node was created\n");
        return 1;
    }

    const std::string existing = "/tmp/kin_existing_sys_node_" + std::to_string((long)getpid());
    {
        FILE* f = std::fopen(existing.c_str(), "w");
        if (!f) return 1;
        std::fputs("old", f);
        std::fclose(f);
    }
    if (!utils.FileWriteExisting(existing.c_str(), "new")) {
        std::fprintf(stderr, "existing node write failed\n");
        return 1;
    }
    if (!utils.FileWriteIfChangedBlocking(existing.c_str(), "new")) {
        std::fprintf(stderr, "same-value write should be treated as success\n");
        return 1;
    }
    if (!utils.FileWriteIfChangedBlocking(existing.c_str(), "changed")) {
        std::fprintf(stderr, "changed-value write failed\n");
        return 1;
    }
    FILE* f = std::fopen(existing.c_str(), "r");
    char buf[8] = {};
    if (!f || !std::fgets(buf, sizeof(buf), f)) return 1;
    if (f) std::fclose(f);
    unlink(existing.c_str());
    if (std::strcmp(buf, "changed") != 0) {
        std::fprintf(stderr, "existing node content mismatch: %s\n", buf);
        return 1;
    }
    std::puts("PASS syswrite existing-only semantics");
    return 0;
}
