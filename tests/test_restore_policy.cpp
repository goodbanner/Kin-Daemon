#include "RestorePolicy.hpp"
#include <cstdio>
int main() {
    if (kin::releaseOwned(false, true, false)) return 1;
    if (kin::releaseOwned(true, false, true)) return 2;
    if (!kin::releaseOwned(true, true, false)) return 3;
    if (kin::releaseOwned(true, true, true)) return 4;
    std::puts("PASS restore policy: retry failed restore, release external ownership");
}
