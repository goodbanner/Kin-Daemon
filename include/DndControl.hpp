// 游戏态自动免打扰(Do Not Disturb)
//
// 进入 GAMING 时记下用户当前的 zen_mode 并临时切到「仅优先」, 离开 GAMING / 守护退出时原样还原。
// 为什么值得做: 游戏中的通知横幅会触发合成与重绘, 直接表现为掉帧; 手动开免打扰玩家通常不会做。
//
// 安全约定:
//  - 默认关闭(Config::AutoMode::auto_dnd), 因为它改变用户可见的系统行为;
//  - 只在我们确实改动过的情况下才还原, 绝不去覆盖用户自己设的值;
//  - 读不到当前值(命令失败/无 settings 二进制)就完全不动作。
//
// 参考思路: Encore Tweaks 的游戏免打扰(其 Main.cpp 里 prev_dnd_state 的保存/恢复语义)。
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kin {

class DndControl {
public:
    /** zen_mode 取值: 0=关闭 1=仅优先 2=仅闹钟 3=完全静音 4=仅闹钟(勿扰) */
    static constexpr int kPriorityOnly = 1;

    /**
     * 进入游戏态: 首次调用时记录用户当前 zen_mode, 再切到 mode。
     * 已经是我们改过的状态时重复调用无副作用(不会把临时值当成"用户原值")。
     */
    void engage(int mode = kPriorityOnly) {
        if (engaged_) return;
        const int cur = readZenMode();
        if (cur < 0) return;                        // 读不到就别乱改
        if (cur == mode) return;                    // 本来就是目标值: 不动, 也不记
        if (!writeZenMode(mode)) return;            // 写失败: 不标记, 下次再试
        saved_ = cur;
        savedValid_ = true;
        engaged_ = true;
    }

    /** 离开游戏态/关闭开关/守护退出: 还原记录的原值。只在我们改过时才写。 */
    void release() {
        if (!engaged_) { savedValid_ = false; return; }
        if (savedValid_) writeZenMode(saved_);
        engaged_ = false;
        savedValid_ = false;
    }

    bool engaged() const { return engaged_; }
    int  savedZenMode() const { return savedValid_ ? saved_ : -1; }

    /** 读当前 zen_mode; -1 = 读不到。 */
    static int readZenMode() {
        FILE* fp = popen("settings get global zen_mode 2>/dev/null", "r");
        if (!fp) return -1;
        char b[32] = {0};
        const size_t n = fread(b, 1, sizeof(b) - 1, fp);
        pclose(fp);
        if (n == 0) return -1;
        for (size_t i = 0; i < n; ++i)
            if (b[i] == '\n' || b[i] == '\r') { b[i] = '\0'; break; }
        if (std::strcmp(b, "null") == 0) return 0;   // 从未设置过 → 视为关闭
        return std::atoi(b);
    }

    static bool writeZenMode(int mode) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "settings put global zen_mode %d", mode);
        FILE* fp = popen(cmd, "r");
        if (!fp) return false;
        return pclose(fp) == 0;
    }

private:
    bool engaged_ = false;      // 当前是否由我们接管着
    bool savedValid_ = false;   // 是否记下了用户原值
    int  saved_ = 0;
};

} // namespace kin
