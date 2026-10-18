#include "WifiPowerSave.hpp"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct Io {
    std::string iw = "off", node = "0";
    bool fail = false, unreadable = false, ignoreWrite = false;
    int iwWrites = 0, nodeWrites = 0;
    std::string readIw() { return unreadable ? "" : iw; }
    std::string readNode() { return unreadable ? "" : node; }
    bool writeIw(const std::string& v) { ++iwWrites; if (fail) return false; if (!ignoreWrite) iw = v; return true; }
    bool writeNode(const std::string& v) { ++nodeWrites; if (fail) return false; if (!ignoreWrite) node = v; return true; }
};
int main() {
    CHECK(kin::parseWifiPowerSave("Power save: on\n") == "on");
    CHECK(kin::parseWifiPowerSave("Power save: off\r\n") == "off");
    CHECK(kin::parseWifiPowerSave("command failed: on").empty());
    kin::WifiPowerSave wifi;
    Io io;
    CHECK(wifi.apply("auto", true, io) && io.iwWrites == 0);
    CHECK(wifi.apply("on", true, io) && io.iw == "on" && io.nodeWrites == 0);
    CHECK(wifi.apply("auto", true, io) && io.iw == "off");
    io.iw = "on";
    CHECK(wifi.apply("off", true, io) && io.iw == "off");
    CHECK(wifi.apply("auto", true, io) && io.iw == "on");
    CHECK(wifi.apply("off", true, io));
    CHECK(wifi.apply("on", true, io));
    CHECK(wifi.apply("auto", true, io) && io.iw == "on");
    CHECK(wifi.apply("off", true, io));
    io.fail = true;
    CHECK(!wifi.apply("auto", false, io));
    io.fail = false;
    io.unreadable = true;
    CHECK(!wifi.apply("auto", false, io));
    io.unreadable = false;
    CHECK(wifi.retryRestore(io) && io.iw == "on");
    int afterRestore = io.iwWrites;
    CHECK(wifi.retryRestore(io) && io.iwWrites == afterRestore);
    CHECK(wifi.apply("off", true, io));
    io.iw = "on";
    int before = io.iwWrites;
    CHECK(wifi.apply("auto", true, io) && io.iwWrites == before);
    io.fail = true;
    CHECK(!wifi.apply("off", true, io) && io.nodeWrites == 0);
    before = io.iwWrites;
    CHECK(wifi.apply("auto", true, io) && io.iwWrites == before);
    io.fail = false;
    io.iw.clear();
    CHECK(wifi.apply("on", true, io) && io.node == "1");
    io.iw = "off"; // A newly available backend must not steal the restore snapshot.
    CHECK(wifi.apply("off", true, io) && io.node == "0" && io.iwWrites == before);
    CHECK(wifi.apply("auto", false, io) && io.node == "0");
    io.iw.clear(); io.node.clear();
    before = io.nodeWrites;
    CHECK(!wifi.apply("on", true, io) && io.nodeWrites == before);
    CHECK(!wifi.apply("invalid", true, io));
    io.iw = "off";
    io.ignoreWrite = true;
    CHECK(!wifi.apply("on", true, io));
    io.ignoreWrite = false;
    CHECK(wifi.apply("on", true, io));
    io.ignoreWrite = true;
    CHECK(!wifi.apply("auto", true, io));
    io.ignoreWrite = false;
    CHECK(wifi.apply("auto", true, io) && io.iw == "off");
    CHECK(wifi.apply("on", true, io));
    io.fail = true;
    CHECK(!wifi.apply("auto", false, io));
    io.fail = false;
    CHECK(wifi.apply("on", true, io)); // New policy cancels a stale restore request.
    CHECK(wifi.retryRestore(io) && io.iw == "on");
    std::puts("PASS: WiFi transitions, disable, retry, external changes and missing capabilities");
}
