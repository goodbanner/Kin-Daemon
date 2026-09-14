#pragma once
#include <string>
#include <string_view>

namespace kin {
class WindowFocus {
public:
    void consume(std::string_view line) {
        auto key = line.find("mFocusedApp=");
        const bool focused = key != line.npos;
        if (!focused) key = line.find("mCurrentFocus=");
        if (key == line.npos) return;
        auto user = line.find(" u", key);
        if (user == line.npos) return;
        auto pos = user + 2;
        const auto start = pos;
        while (pos < line.size() && line[pos] >= 48 && line[pos] <= 57) ++pos;
        if (pos == start || pos >= line.size() || line[pos++] != 32) return;
        auto slash = line.find("/", pos);
        if (slash == line.npos) return;
        auto pkg = line.substr(pos, slash - pos);
        if (pkg.empty() || pkg.size() > 255 || pkg.find(".") == pkg.npos) return;
        if (pkg.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._") != pkg.npos) return;
        auto& dest = focused ? focused_ : current_;
        if (dest.empty()) dest = pkg;
    }
    const std::string& package() const { return focused_.empty() ? current_ : focused_; }
private:
    std::string focused_;
    std::string current_;
};
}
