#include "NodeWriter.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    const auto path = (std::filesystem::temp_directory_path() /
                       ("kin-node-writer-" + std::to_string(getpid()))).string();
    std::remove(path.c_str());
    if (kin::writeChangedNode(path.c_str(), "1").code != kin::NodeWriteCode::ReadFailed) return 1;
    { std::ofstream out(path); out << "1\n"; }
    if (kin::writeChangedNode(path.c_str(), "1").code != kin::NodeWriteCode::Unchanged) return 2;
    auto changed = kin::writeChangedNode(path.c_str(), "2", false, true);
    if (!changed.ok()) return 3;
    std::ifstream in(path); std::string value; in >> value;
    std::remove(path.c_str());
    if (value != "2") return 4;
    std::puts("PASS node writer: existing-only, unchanged, write and readback");
}
