#pragma once
#include <string>

namespace kin {
// Callers serialize access and supply raw, trimmed reads and checked writes.
class OwnedNode {
public:
    template<class Read, class Write>
    bool apply(const std::string& value, Read read, Write write) {
        const std::string current = read();
        if (current.empty()) return false;
        if (held_ && current != last_) held_ = false;
        if (current == value) return true;
        if (!write(value)) return false;
        if (!held_) original_ = current;
        const std::string applied = read();
        last_ = applied.empty() ? value : applied;
        held_ = true;
        return true;
    }

    template<class Read, class Write>
    bool restore(Read read, Write write) {
        if (!held_) return true;
        const std::string current = read();
        if (current.empty()) return false;
        if (current == last_ && !write(original_)) return false;
        held_ = false;
        original_.clear();
        last_.clear();
        return true;
    }

    bool held() const noexcept { return held_; }

private:
    bool held_ = false;
    std::string original_;
    std::string last_;
};
}
