#include "FreqWriteOrder.hpp"
#include <cstdio>

int main() {
    // 智能档把上限压到 800MHz 后, 风驰下限 1.2GHz 必须先抬上限。
    if (!kin::writeMaxBeforeMin(300000, 800000, 1200000, 3628800)) {
        std::puts("FAIL raising floor above live ceiling must write max first");
        return 1;
    }
    // 从高频地板降到更低的上限: 先写下限, 否则新上限低于当前下限会被拒绝。
    if (kin::writeMaxBeforeMin(2000000, 3300000, 384000, 1400000)) {
        std::puts("FAIL lowering ceiling below live floor must write min first");
        return 1;
    }
    // 读不到当前节点时保持旧顺序。
    if (kin::writeMaxBeforeMin(0, 0, 1200000, 3628800)) {
        std::puts("FAIL unknown current values must keep min-then-max");
        return 1;
    }
    // 新下限没有越过当前上限, 不改顺序。
    if (kin::writeMaxBeforeMin(384000, 2361600, 787200, 3129600)) {
        std::puts("FAIL non-overlapping raise must keep min-then-max");
        return 1;
    }
    // 刚好等于当前上限时, 先写下限仍然合法。
    if (kin::writeMaxBeforeMin(384000, 1200000, 1200000, 3000000)) {
        std::puts("FAIL equal floor and ceiling must keep min-then-max");
        return 1;
    }
    std::puts("PASS freq write order");
    return 0;
}
