#pragma once

#include <mutex>
#include <thread>

namespace kin {
enum class WriteStatus { Unknown, Written, Unchanged, Failed };
inline const char* writeStatusName(WriteStatus status) noexcept {
    switch (status) {
        case WriteStatus::Written: return "written";
        case WriteStatus::Unchanged: return "unchanged";
        case WriteStatus::Failed: return "failed";
        default: return "unknown";
    }
}

class WriteStatusTracker {
public:
    void record(WriteStatus status) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = status;
        if (owner_ != std::this_thread::get_id()) return;
        if (status == WriteStatus::Failed || batch_ == WriteStatus::Failed) batch_ = WriteStatus::Failed;
        else if (status == WriteStatus::Written || batch_ == WriteStatus::Written) batch_ = WriteStatus::Written;
        else if (status == WriteStatus::Unchanged && batch_ == WriteStatus::Unknown) batch_ = status;
    }
    void endBatch() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_ == std::this_thread::get_id()) owner_ = {};
    }
    // Caller serializes batch lifetimes; other workers may still record individual results.
    void startBatch() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        batch_ = WriteStatus::Unknown;
        owner_ = std::this_thread::get_id();
    }
    WriteStatus get() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    WriteStatus batch() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return batch_;
    }
private:
    mutable std::mutex mutex_;
    std::thread::id owner_{};
    WriteStatus status_ = WriteStatus::Unknown;
    WriteStatus batch_ = WriteStatus::Unknown;
};
} // namespace kin
