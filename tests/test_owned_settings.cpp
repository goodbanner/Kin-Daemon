#include "OwnedSettings.hpp"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct Io {
    std::map<std::string, std::string> values{{"vm", "60"}, {"tcp", "100"}};
    std::string failPath;
    bool unreadable = false, ignoreWrite = false;
    int reads = 0, writes = 0;
    std::string read(const std::string& path) {
        ++reads;
        const auto it = values.find(path);
        return unreadable || it == values.end() ? "" : it->second;
    }
    bool write(const std::string& path, const std::string& value) {
        ++writes;
        if (path == failPath) return false;
        if (!ignoreWrite) values[path] = value;
        return true;
    }
};
int main() {
    kin::OwnedSettings settings;
    Io io;
    CHECK(settings.restore(io) && io.reads == 0);
    CHECK(!settings.apply("missing", "1", io) && io.writes == 0);
    CHECK(settings.apply("vm", "60", io) && io.writes == 0);
    io.failPath = "vm";
    CHECK(!settings.apply("vm", "35", io));
    int failedWrites = io.writes;
    CHECK(settings.restore(io) && io.writes == failedWrites);
    io.failPath.clear();
    CHECK(settings.apply("vm", "35", io));
    CHECK(settings.apply("vm", "12", io));
    CHECK(settings.apply("tcp", "200", io));
    io.failPath = "vm";
    CHECK(!settings.restore(io) && io.values["tcp"] == "100");
    io.failPath.clear(); io.unreadable = true;
    CHECK(!settings.retryRestore(io));
    io.unreadable = false;
    CHECK(settings.retryRestore(io) && io.values["vm"] == "60");
    int reads = io.reads;
    CHECK(settings.retryRestore(io) && io.reads == reads);
    CHECK(settings.apply("vm", "35", io));
    io.values["vm"] = "80";
    int writes = io.writes;
    CHECK(settings.restore(io) && io.values["vm"] == "80" && io.writes == writes);
    CHECK(settings.apply("vm", "35", io));
    io.ignoreWrite = true;
    CHECK(!settings.restore(io));
    io.ignoreWrite = false;
    CHECK(settings.retryRestore(io) && io.values["vm"] == "80");
    CHECK(settings.apply("vm", "35", io));
    io.failPath = "vm";
    CHECK(!settings.restore(io));
    io.failPath.clear();
    CHECK(settings.apply("vm", "12", io));
    CHECK(settings.retryRestore(io) && io.values["vm"] == "12");
    CHECK(settings.restore(io) && io.values["vm"] == "80");
    io.ignoreWrite = true;
    CHECK(!settings.apply("vm", "12", io));
    io.ignoreWrite = false;
    CHECK(settings.restore(io) && io.values["vm"] == "80");
    std::puts("PASS: multi-node ownership, partial restore, retry, external writes and re-enable");
}
