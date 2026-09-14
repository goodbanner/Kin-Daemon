#include "OwnedNode.hpp"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    kin::OwnedNode node;
    std::string current = "20";
    bool fail = false, unreadable = false;
    int writes = 0;
    auto read = [&] { return unreadable ? std::string() : current; };
    auto write = [&](const std::string& v) { ++writes; if (fail) return false; current = v; return true; };
    CHECK(node.restore(read, write) && writes == 0);
    CHECK(node.apply("20", read, write) && !node.held() && writes == 0);
    fail = true;
    CHECK(!node.apply("30", read, write) && !node.held());
    CHECK(node.restore(read, write) && writes == 1);
    fail = false;
    CHECK(node.apply("30", read, write) && node.held());
    CHECK(node.apply("40", read, write));
    fail = true;
    CHECK(!node.restore(read, write) && node.held());
    fail = false;
    unreadable = true;
    CHECK(!node.restore(read, write) && node.held());
    unreadable = false;
    CHECK(node.restore(read, write) && current == "20" && !node.held());
    CHECK(node.apply("30", read, write));
    current = "50";
    const int before = writes;
    CHECK(node.restore(read, write) && current == "50" && writes == before);
    CHECK(node.apply("30", read, write));
    CHECK(node.restore(read, write) && current == "50");
    auto formattedWrite = [&](const std::string& v) { current = v == "12" ? "12.00" : v; return true; };
    CHECK(node.apply("12", read, formattedWrite));
    CHECK(node.restore(read, formattedWrite) && current == "50");
    std::puts("PASS: owned node no-op, failed write, retry, external ownership, readback and reacquire");
}
