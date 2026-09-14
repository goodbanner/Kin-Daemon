#include "NodeValue.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("kin-node-value-" + std::to_string(getpid()));
    if (!std::filesystem::create_directory(dir)) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{dir};
    const auto path = (dir / "node").string();
    const auto check = [&](const std::string& value, const std::string& wanted,
                           kin::NodeComparison expected, const char* name) {
        { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << value; }
        const auto actual = kin::compareNodeValue(path.c_str(), wanted);
        if (actual != expected) {
            std::fprintf(stderr, "FAIL %s: expected %d, got %d\n", name, int(expected), int(actual));
            return false;
        }
        return true;
    };
    using C = kin::NodeComparison;
    if (kin::compareNodeValue(path.c_str(), "1") != C::Unreadable ||
        std::filesystem::exists(path)) return 2;
    if (!check("100\n", "100", C::Equal, "sysfs newline") ||
        !check("100 \t\r\n", "100", C::Equal, "trailing whitespace") ||
        !check(" 100", "100", C::Different, "leading whitespace preserved") ||
        !check("", "100", C::Different, "empty node") ||
        !check("200", "100", C::Different, "external drift") ||
        !check(std::string(255, 'a') + "x", std::string(255, 'a'), C::Different,
               "matching truncated prefix must not skip write") ||
        !check(std::string(4096, 'a'), std::string(4096, 'a'), C::Equal, "read boundary") ||
        !check(std::string(4097, 'a'), std::string(4096, 'a'), C::TooLong, "bounded read")) return 3;
    std::puts("PASS node value: missing, whitespace, empty, drift, truncation and bounds");
}
