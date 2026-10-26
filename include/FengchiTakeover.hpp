#pragma once
// 风驰游戏(game_heavy)前台接管的纯决策。调度器只按这里的边沿做事:
// 进入后禁止写 scaling_min/max 与 governor, 退出后才允许把基础档写回去。
#include <atomic>
#include <cstddef>
#include <string>

namespace kin {

inline std::atomic<int> fengchiHandsOff{0};

enum class FengchiPhase { Idle, Active };

struct FengchiEdge {
    FengchiPhase phase = FengchiPhase::Idle;
    bool entered = false;
    bool exited = false;
    bool suppressCpuWrites = false;
    bool enableOiface = false;
    bool restoreOiface = false;
    bool restoreBase = false;
    const char* log = nullptr; // "风驰接管" 或 "恢复基础档"
};

inline FengchiEdge stepFengchi(FengchiPhase phase, bool gameHeavyForeground) {
    FengchiEdge edge;
    if (gameHeavyForeground) {
        edge.phase = FengchiPhase::Active;
        edge.suppressCpuWrites = true;
        if (phase != FengchiPhase::Active) {
            edge.entered = true;
            edge.enableOiface = true;
            edge.log = "风驰接管";
        }
        return edge;
    }
    edge.phase = FengchiPhase::Idle;
    if (phase == FengchiPhase::Active) {
        edge.exited = true;
        edge.restoreBase = true;
        edge.restoreOiface = true;
        edge.log = "恢复基础档";
    }
    return edge;
}

// 进入时记下 persist.sys.oiface.enable 与 init.svc.oiface, 退出时按原值 setprop 并 start/stop。
// 调用方负责把读到的属性收成纯数字再送进来; 这里再次只接受最多 8 位数字。
class OifaceTakeover {
public:
    template <class Ops>
    void enter(Ops& ops) {
        if (engaged_) return;
        savedEnable_ = sanitizeEnable(ops.readEnable());
        savedRunning_ = ops.readRunning();
        ops.setEnable("2");
        ops.start();
        engaged_ = true;
    }

    template <class Ops>
    void exit(Ops& ops) {
        if (!engaged_) return;
        ops.setEnable(savedEnable_.c_str());
        if (savedRunning_) ops.start();
        else ops.stop();
        engaged_ = false;
    }

    bool engaged() const { return engaged_; }

    const std::string& savedEnable() const { return savedEnable_; }
    bool savedRunning() const { return savedRunning_; }

    static std::string sanitizeEnable(const std::string& raw) {
        std::string out;
        for (unsigned char c : raw) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            if (c < '0' || c > '9') return {};
            if (out.size() >= 8) return {};
            out.push_back(static_cast<char>(c));
        }
        return out;
    }

private:
    bool engaged_ = false;
    bool savedRunning_ = false;
    std::string savedEnable_;
};

} // namespace kin
