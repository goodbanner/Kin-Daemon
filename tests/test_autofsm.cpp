// AutoFsm 主机单测: 覆盖本轮从上游 ASB V64 移植的两条行为。
//   1. GAMING 需要包名表同意 —— 前台已明确分类为非游戏时, GPU 单指标不再进 GAMING。
//   2. MODERATE↔HEAVY 相邻档位对多要一拍确认(抖动抑制)。
// 用 host_posix_shim.hpp 让 Logger→Utils 的 POSIX 依赖在 mingw 下可编译;
// 节点读取在本机全部失败 = "传感器不可用" 分支, 正是 FSM 需要覆盖的路径。
#include "host_posix_shim.hpp"
#include "AutoFsm.hpp"
#include <cstdio>

namespace {

struct Tick {
    int gpu = 10;
    int cpu = 70;
    int temp = 35;
    int screen = 1;
    bool gameHint = false;
    bool fgNotGame = false;
    bool disputed = false;
};

bool step(AutoFsm& fsm, const Tick& t) {
    return fsm.update(t.gpu, 1.0f, t.temp, t.screen, t.cpu, t.gameHint, -1, false, t.fgNotGame,
                      t.disputed);
}

// 反复喂同一组指标, 返回第几拍到达目标状态(0 = 一直没到)。
int ticksToReach(AutoFsm& fsm, int target, const Tick& t, int maxTicks = 20) {
    for (int i = 1; i <= maxTicks; ++i) {
        step(fsm, t);
        if (fsm.state() == target) return i;
    }
    return 0;
}

}  // namespace

int main() {
    Logger logger;

    // ── 用例 1: LIGHT_IDLE → MODERATE 走普通升档窗口(UP_WINDOW=2) ──
    {
        AutoFsm fsm(logger);
        Tick t; t.cpu = 70; t.gpu = 10;
        const int n = ticksToReach(fsm, AutoFsm::MODERATE, t);
        if (n != 3) { std::printf("FAIL moderate window: %d\n", n); return 1; }
    }

    // ── 用例 2: MODERATE → HEAVY 需要 3 拍确认(上游 V64 相邻档位对规则), 而非 2 拍 ──
    {
        AutoFsm fsm(logger);
        Tick mid; mid.cpu = 70; mid.gpu = 10;
        if (ticksToReach(fsm, AutoFsm::MODERATE, mid) != 3) return 2;
        Tick heavy; heavy.cpu = 90; heavy.gpu = 10;
        const int n = ticksToReach(fsm, AutoFsm::HEAVY, heavy);
        // 未移植时是 3 拍(window=2); 移植后是 4 拍(window=3)。
        if (n != 4) { std::printf("FAIL pair confirm window: %d\n", n); return 3; }
    }

    // ── 用例 3: 前台已分类为非游戏 → GPU 95% 也不进 GAMING ──
    {
        AutoFsm fsm(logger);
        Tick t; t.gpu = 95; t.cpu = 20; t.fgNotGame = true;
        for (int i = 0; i < 20; ++i) {
            step(fsm, t);
            if (fsm.state() == AutoFsm::GAMING) {
                std::puts("FAIL gated gaming: entered GAMING on GPU alone"); return 4;
            }
        }
        // 该场景应停在 MODERATE(GPU 重载但 CPU 未参与), 而不是被抬到 GAMING。
        if (fsm.state() != AutoFsm::MODERATE) {
            std::printf("FAIL gated gaming: state=%d\n", fsm.state()); return 5;
        }
    }

    // ── 用例 4: 同样分类为非游戏, 但 CPU 真吃满 → 仍给 HEAVY(按 CPU 评级) ──
    {
        AutoFsm fsm(logger);
        Tick t; t.gpu = 95; t.cpu = 90; t.fgNotGame = true;
        const int n = ticksToReach(fsm, AutoFsm::HEAVY, t);
        if (n == 0 || fsm.state() == AutoFsm::GAMING) {
            std::printf("FAIL gated gaming heavy path: n=%d state=%d\n", n, fsm.state()); return 6;
        }
    }

    // ── 用例 5: 类别未知(fgNotGame=false) → 保持旧行为, GPU 高仍能进 GAMING ──
    {
        AutoFsm fsm(logger);
        Tick t; t.gpu = 95; t.cpu = 70;
        const int n = ticksToReach(fsm, AutoFsm::GAMING, t);
        if (n == 0) { std::printf("FAIL ungated gaming: state=%d\n", fsm.state()); return 7; }
    }

    // ── 用例 6: gameHint(画像命中游戏) → 即时进 GAMING, 不受包名否证影响 ──
    {
        AutoFsm fsm(logger);
        Tick t; t.gpu = 10; t.cpu = 20; t.gameHint = true; t.fgNotGame = true;
        step(fsm, t);
        if (fsm.state() != AutoFsm::GAMING) {
            std::printf("FAIL game hint: state=%d\n", fsm.state()); return 8;
        }
    }

    // ── 用例 7: 热源争议 → 负载驱动的迁移再多一拍(MODERATE→HEAVY 变 5 拍) ──
    {
        AutoFsm fsm(logger);
        Tick mid; mid.cpu = 70; mid.gpu = 10; mid.disputed = true;
        if (ticksToReach(fsm, AutoFsm::MODERATE, mid) != 4) return 9;   // 2 拍窗口 + 1
        Tick heavy; heavy.cpu = 90; heavy.gpu = 10; heavy.disputed = true;
        const int n = ticksToReach(fsm, AutoFsm::HEAVY, heavy);
        // 3 拍相邻档位窗口 + 1 拍争议 = 5
        if (n != 5) { std::printf("FAIL disputed window: %d\n", n); return 10; }
    }

    // ── 用例 8: 争议不能拖慢热升档(真烫必须立刻降档) ──
    {
        AutoFsm fsm(logger);
        // 60℃ ≥ warmup_bypass_temp(60), 预热宽限不生效; ≥ fsm_sustained_enter(54) 且重载 → 热升 SUSTAINED。
        Tick mid; mid.cpu = 70; mid.gpu = 10; mid.temp = 60; mid.disputed = true;
        if (ticksToReach(fsm, AutoFsm::MODERATE, mid) != 4) return 11;
        // 真重载(cpu 90 → HEAVY) 再加温 → 热升 SUSTAINED。
        Tick hot; hot.cpu = 90; hot.gpu = 10; hot.temp = 60; hot.disputed = true;
        const int n = ticksToReach(fsm, AutoFsm::SUSTAINED, hot);
        // 热升档走普通升档窗口(2 拍), 既不吃相邻档位对规则也不吃争议 +1。
        if (n != 3) { std::printf("FAIL thermal escalation slowed by dispute: %d\n", n); return 12; }
    }

    std::puts("PASS autofsm: pair confirm window, gaming package gate, hint priority, disputed sensor");
    return 0;
}
