#pragma once
#include "OwnedNode.hpp"
#include <string_view>

namespace kin {
inline std::string parseWifiPowerSave(std::string_view output) {
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' ||
                               output.back() == ' ' || output.back() == '\t'))
        output.remove_suffix(1);
    if (output == "Power save: on") return "on";
    if (output == "Power save: off") return "off";
    return {};
}

class WifiPowerSave {
public:
    template<class Io>
    bool apply(std::string_view mode, bool enabled, Io& io) {
        auto ri = [&] { return io.readIw(); };
        auto wi = [&](const std::string& v) { return io.writeIw(v); };
        auto rn = [&] { return io.readNode(); };
        auto wn = [&](const std::string& v) { return io.writeNode(v); };
        if (!enabled || mode == "auto") {
            const bool a = iw_.restore(ri, [&](const std::string& v) { return wi(v) && ri() == v; });
            const bool b = node_.restore(rn, [&](const std::string& v) { return wn(v) && rn() == v; });
            restorePending_ = !(a && b);
            return a && b;
        }
        if (mode != "on" && mode != "off") return false;
        restorePending_ = false;
        // Interfaces may alias one setting: keep one backend per ownership window.
        const std::string value = mode == "on" ? "1" : "0";
        if (node_.held()) return node_.apply(value, rn, wn) && rn() == value;
        if (iw_.held() || !ri().empty()) return iw_.apply(std::string(mode), ri, wi) && ri() == mode;
        return node_.apply(value, rn, wn) && rn() == value;
    }
    template<class Io>
    bool retryRestore(Io& io) {
        return !restorePending_ || apply("auto", false, io);
    }
private:
    bool restorePending_ = false;
    OwnedNode iw_;
    OwnedNode node_;
};
}
