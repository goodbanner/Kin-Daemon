#include "CpuTempGuard.hpp"
#include <cstdio>

static int fails = 0;

static void expect(const char* name, const kin::CpuTempGuard& got, int published, int baseline, int streak) {
    if (got.published == published && got.baseline == baseline && got.streak == streak) {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s: published=%d baseline=%d streak=%d want %d/%d/%d\n",
                name, got.published, got.baseline, got.streak, published, baseline, streak);
    ++fails;
}

int main() {
    expect("normal first sample", kin::acceptCpuTemp(40, 38, -1, 0, 80), 40, 40, 0);
    expect("two sensors agree when hot", kin::acceptCpuTemp(90, 88, -1, 0, 80), 90, 90, 0);
    expect("cold isolated rejected once", kin::acceptCpuTemp(97, -1, -1, 0, 80), -1, -1, 1);
    expect("cold isolated accepted on second tick", kin::acceptCpuTemp(97, -1, -1, 1, 80), 97, 97, 0);
    expect("spike rejected", kin::acceptCpuTemp(90, 40, 40, 0, 80), 40, 40, 1);
    expect("spike accepted on second tick", kin::acceptCpuTemp(91, 42, 40, 1, 80), 91, 91, 0);
    expect("endorsed rise is not a spike", kin::acceptCpuTemp(70, 60, 40, 0, 80), 70, 70, 0);
    expect("glitch then normal clears streak", kin::acceptCpuTemp(42, 41, -1, 1, 80), 42, 42, 0);
    expect("no reading keeps baseline", kin::acceptCpuTemp(-1, -1, 40, 1, 80), 40, 40, 1);
    expect("below vhot cold start is accepted", kin::acceptCpuTemp(70, -1, -1, 0, 80), 70, 70, 0);
    if (fails) {
        std::printf("FAIL cpu temp guard: %d\n", fails);
        return 1;
    }
    std::puts("PASS cpu temp guard");
    return 0;
}
