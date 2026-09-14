#include "PolicyResolver.hpp"
#include <cstdio>

static bool same(const kin::PolicyCaps& c, int l0, int l1, int b0, int b1) {
    return c.minL == l0 && c.maxL == l1 && c.minB == b0 && c.maxB == b1;
}

int main() {
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.cap(800, 1500, "thermal", "skin_hot");
        if (!same(r.result().caps, 300, 800, 400, 1500) ||
            r.result().policy != "thermal" || r.result().reason != "skin_hot") return 1;
        r.cap(900, 1800, "battery", "low_battery");
        if (!same(r.result().caps, 300, 800, 400, 1500)) return 2;
    }
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.appPolicy({900, 1200, 500, 1100}, "game", "profile");
        r.cap(700, 1000, "thermal", "junction");
        if (!same(r.result().caps, 700, 700, 500, 1000)) return 3;
        if (r.result().policy != "thermal") return 4;
    }
    {
        // Safety must win even when registered before the app policy.
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.cap(650, 900, "battery", "critical_battery");
        r.appPolicy({900, 1500, 500, 1800}, "game", "profile");
        if (!same(r.result().caps, 650, 650, 500, 900)) return 5;
    }
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.appPolicy({300, 1500, 400, 2500}, "boost", "jank");
        if (!same(r.result().caps, 300, 1500, 400, 2500)) {
            std::fprintf(stderr, "FAIL: base FSM cap must not become an implicit safety cap\n");
            return 6;
        }
    }
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.cap(650, 900, "thermal", "junction");
        r.appPolicy({300, 1500, 400, 2500}, "game", "profile");
        if (r.result().policy != "thermal" || r.result().reason != "junction") {
            std::fprintf(stderr, "FAIL: diagnostics must retain the binding safety reason\n");
            return 7;
        }
    }
    {
        // Producer order must not change caps or the binding safety reason.
        for (int order = 0; order < 2; ++order) {
            kin::PolicyResolver r({300, 1000, 400, 2000});
            if (order == 0) r.appPolicy({300, 1600, 400, 2600}, "game", "profile");
            r.cap(850, 1200, "battery", "low_battery");
            r.cap(700, 1000, "thermal", "junction");
            r.cap(900, 1400, "fas", "frame_target");
            if (order == 1) r.appPolicy({300, 1600, 400, 2600}, "game", "profile");
            if (!same(r.result().caps, 300, 700, 400, 1000) ||
                r.result().policy != "thermal" || r.result().reason != "junction") return 8;
        }
    }
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.cap(1200, 2500, "thermal", "not_binding");
        r.cap(0, -1, "battery", "invalid");
        if (!same(r.result().caps, 300, 1000, 400, 2000) ||
            r.result().policy != "base") return 9;
        // New ticks do not inherit old thermal/battery limits after their exit.
        kin::PolicyResolver next({300, 1500, 400, 2500});
        if (!same(next.result().caps, 300, 1500, 400, 2500)) return 10;
    }
    {
        // App/category and FAS must retain separate provenance when limiting different clusters.
        for (int order = 0; order < 2; ++order) {
            kin::PolicyResolver r({300, 1500, 400, 2500});
            if (order == 0) r.cap(700, 2200, "app", "category");
            r.cap(1000, 1200, "fas", "frame_target");
            if (order == 1) r.cap(700, 2200, "app", "category");
            r.appPolicy({300, 1800, 400, 2800}, "boost", "jank");
            if (!same(r.result().caps, 300, 700, 400, 1200) ||
                r.result().policy != "app" || r.result().reason != "category") {
                std::puts("FAIL: shared cap slot loses source priority/provenance");
                return 11;
            }
        }
    }
    {
        // A nonbinding source must never steal another source's limiting reason.
        kin::PolicyResolver r({300, 1500, 400, 2500});
        r.cap(700, 1200, "fas", "frame_target");
        r.cap(900, 2000, "app", "category");
        r.appPolicy({300, 1800, 400, 2800}, "boost", "jank");
        if (r.result().policy != "fas") return 12;
    }
    {
        // Scheduler's candidate already includes caps before the final merge.
        kin::PolicyResolver r({300, 1500, 400, 2500});
        r.cap(0, 1200, "thermal", "joint_dvfs");
        r.appPolicy({300, 1500, 400, 1200}, "game", "state");
        if (r.result().policy != "thermal" || r.result().reason != "joint_dvfs") {
            std::puts("FAIL: pre-applied cap loses diagnostic attribution");
            return 13;
        }
    }
    {
        kin::PolicyResolver r({300, 1000, 400, 2000});
        r.appPolicy({900, -1, 500, 100}, "bad", "config");
        if (!same(r.result().caps, 300, 1000, 100, 100)) {
            std::puts("FAIL: invalid app caps should fall back to safe base bounds");
            return 14;
        }
    }
    {
        kin::PolicyResolver r({300, 2000, 400, 3000});
        r.appPolicy({300, 2000, 400, 3000}, "game", "profile");
        r.cap(1500, 1800, "fas", "frame_target");
        r.cap(1200, 1400, "power", "jank_boost");
        r.cap(1000, 1100, "low_battery", "low_battery");
        r.appPolicy({300, 2500, 400, 3500}, "game", "profile");
        if (!same(r.result().caps, 300, 1000, 400, 1100) ||
            r.result().policy != "low_battery" || r.result().reason != "low_battery") return 15;
    }
    {
        // Equal binding caps use fixed safety priority, regardless of registration order.
        kin::PolicyResolver r({300, 2000, 400, 3000});
        r.cap(1000, 0, "fas", "frame_target");
        r.cap(1000, 2500, "thermal", "junction");
        if (r.result().policy != "thermal" || r.result().reason != "junction") return 16;
    }
    {
        kin::PolicyResolver r({300, 2000, 400, 3000});
        r.cap(1000, 2500, "thermal", "junction");
        r.cap(1000, 0, "fas", "frame_target");
        if (r.result().policy != "thermal" || r.result().reason != "junction") return 17;
    }
    std::puts("PASS: policy resolver caps, reasons, order and per-tick reset");
    return 0;
}
