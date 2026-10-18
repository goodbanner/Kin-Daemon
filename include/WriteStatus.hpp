#pragma once

#include <cstddef>
#include <mutex>
#include <string>
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

// 批内节点写入计数。只统计调用方显式给出节点路径的记录, 旧的 record(status) 不计入。
struct WriteNodeCounts {
    unsigned written = 0;
    unsigned unchanged = 0;
    unsigned failed = 0;
    unsigned unknown = 0;
    unsigned dropped = 0;   // 失败节点路径超出上限而未保存的个数
    unsigned total() const noexcept { return written + unchanged + failed + unknown; }
};

class WriteStatusTracker {
public:
    static constexpr std::size_t kMaxFailedNodes = 4;
    static constexpr std::size_t kMaxNodePath = 96;

    void record(WriteStatus status) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        applyLocked(status, nullptr);
    }
    // 已知节点路径时用这个: 批内按节点累计结果, 失败节点路径保留给诊断。
    void recordNode(const char* path, WriteStatus status) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        applyLocked(status, path);
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
        resetNodesLocked();
    }
    WriteStatus get() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    WriteStatus batch() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return batch_;
    }
    WriteNodeCounts batchNodes() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_;
    }
    // 形如 "total=3,written=2,unchanged=1,failed=0"; 本批没有节点记录时返回空串。
    std::string batchNodeSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.total() == 0) return std::string();
        std::string out = "total=" + std::to_string(nodes_.total()) +
                          ",written=" + std::to_string(nodes_.written) +
                          ",unchanged=" + std::to_string(nodes_.unchanged) +
                          ",failed=" + std::to_string(nodes_.failed);
        if (nodes_.unknown) out += ",unknown=" + std::to_string(nodes_.unknown);
        if (nodes_.dropped) out += ",dropped=" + std::to_string(nodes_.dropped);
        return out;
    }
    // 批内失败节点路径(按记录顺序, 最多 kMaxFailedNodes 个, 逗号分隔); 无失败时为空串。
    std::string batchFailedNodes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out;
        for (std::size_t i = 0; i < failedCount_; ++i) {
            if (!out.empty()) out += ',';
            out += failedPaths_[i];
        }
        return out;
    }
private:
    void applyLocked(WriteStatus status, const char* path) noexcept {
        status_ = status;
        if (owner_ != std::this_thread::get_id()) return;
        if (status == WriteStatus::Failed || batch_ == WriteStatus::Failed) batch_ = WriteStatus::Failed;
        else if (status == WriteStatus::Written || batch_ == WriteStatus::Written) batch_ = WriteStatus::Written;
        else if (status == WriteStatus::Unchanged && batch_ == WriteStatus::Unknown) batch_ = status;
        if (!path || !*path) return;
        switch (status) {
            case WriteStatus::Written: ++nodes_.written; break;
            case WriteStatus::Unchanged: ++nodes_.unchanged; break;
            case WriteStatus::Unknown: ++nodes_.unknown; break;
            default: ++nodes_.failed; addFailedLocked(path); break;
        }
    }
    void addFailedLocked(const char* path) noexcept {
        if (failedCount_ >= kMaxFailedNodes) { ++nodes_.dropped; return; }
        char* dst = failedPaths_[failedCount_];
        std::size_t i = 0;
        for (; path[i] && i + 1 < kMaxNodePath; ++i) {
            const char c = path[i];
            // 只保留路径安全字符, 避免外部文本破坏 key=value 状态协议。
            const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '.';
            dst[i] = safe ? c : '_';
        }
        dst[i] = '\0';
        ++failedCount_;
    }
    void resetNodesLocked() noexcept {
        nodes_ = WriteNodeCounts{};
        failedCount_ = 0;
        for (auto& p : failedPaths_) p[0] = '\0';
    }
    mutable std::mutex mutex_;
    std::thread::id owner_{};
    WriteStatus status_ = WriteStatus::Unknown;
    WriteStatus batch_ = WriteStatus::Unknown;
    WriteNodeCounts nodes_{};
    std::size_t failedCount_ = 0;
    char failedPaths_[kMaxFailedNodes][kMaxNodePath] = {};
};
} // namespace kin
