#include "WriteStatus.hpp"
#include <cstdio>
#include <thread>
int main() {
    kin::WriteStatusTracker t;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    t.record(kin::WriteStatus::Written);
    t.record(kin::WriteStatus::Failed);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Failed || t.get() != kin::WriteStatus::Failed) return 1;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Unchanged) return 2;
    t.startBatch();
    t.record(kin::WriteStatus::Unchanged);
    std::thread unrelated([&] { t.record(kin::WriteStatus::Failed); });
    unrelated.join();
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Unchanged) {
        std::puts("FAIL unrelated worker contaminated CPU batch"); return 3;
    }
    t.startBatch();
    t.record(kin::WriteStatus::Failed);
    t.record(kin::WriteStatus::Written);
    t.record(kin::WriteStatus::Unchanged);
    t.endBatch();
    if (t.batch() != kin::WriteStatus::Failed) return 4;
    t.startBatch(); t.endBatch();
    if (t.batch() != kin::WriteStatus::Unknown) return 5;
    std::puts("PASS write status: failure dominance, empty batch, thread isolation");
}
