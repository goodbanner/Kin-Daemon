#pragma once
#include <string>
#include <string_view>

namespace kin {
inline bool validPackageName(std::string_view name) {
    if (name.empty() || name.size() > 255) return false;
    bool start = true;
    for (char c : name) {
        if (c == '.') {
            if (start) return false;
            start = true;
            continue;
        }
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && (start || (c != '_' && !(c >= '0' && c <= '9')))) return false;
        start = false;
    }
    return !start;
}

// Validate at the shell boundary, including callers other than topapp.txt.
template<class Read>
bool readPackageCategory(std::string_view pkg, Read read) {
    if (!validPackageName(pkg)) return false;
    const std::string cmd = "dumpsys package '" + std::string(pkg) +
        "' 2>/dev/null | grep -m1 -iE 'categoryHint=|category='";
    read(cmd.c_str());
    return true;
}
}
