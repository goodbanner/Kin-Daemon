#pragma once
#include "OwnedNode.hpp"
#include <map>

namespace kin {
// In-memory ownership only. Callers serialize access and provide checked I/O.
class OwnedSettings {
public:
    template<class Io>
    bool apply(const std::string& path, const std::string& value, Io& io) {
        pending_ = false;
        auto& node = nodes_[path];
        const bool ok = node.apply(value, [&] { return io.read(path); },
                                   [&](const std::string& v) { return io.write(path, v); });
        if (!node.held()) nodes_.erase(path);
        return ok && io.read(path) == value;
    }

    template<class Io>
    bool restore(Io& io) {
        for (auto it = nodes_.begin(); it != nodes_.end();) {
            const auto& path = it->first;
            const bool ok = it->second.restore([&] { return io.read(path); },
                [&](const std::string& v) { return io.write(path, v) && io.read(path) == v; });
            if (ok) it = nodes_.erase(it);
            else ++it;
        }
        pending_ = !nodes_.empty();
        return !pending_;
    }

    template<class Io>
    bool retryRestore(Io& io) { return !pending_ || restore(io); }

private:
    std::map<std::string, OwnedNode> nodes_;
    bool pending_ = false;
};
}
