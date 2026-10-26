#pragma once
// 风驰游戏(game_heavy)前台接管的纯决策。调度器只按这里的边沿做事:
// 进入后禁止写 scaling_min/max 与 governor, 退出后才允许把基础档写回去。
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace kin {

inline std::atomic<int> fengchiHandsOff{0};

// 进入接管前把原始 oiface 值落到 Kin 数据目录。崩溃后下次启动先按这个文件还原。
inline constexpr const char* kOifaceBackupPath = "/sdcard/Android/Kin/fengchi_oiface.bak";
// 启动把备份写回属性成功时打这一行。内容为空或不是纯数字时打警告, 且不 setprop。
inline constexpr const char* kOifaceBackupRestoredLog = "启动恢复oiface备份";
inline constexpr const char* kOifaceBackupInvalidLog = "oiface备份无效已删除";

enum class OifaceBackupResult {
    Absent,     // 文件不存在
    Restored,   // 已 setprop 且读回一致, 文件已删除
    Kept,       // 内容有效, 但 setprop 或读回失败, 文件保留
    Discarded,  // 内容为空或无效, 未 setprop, 文件已删除
};

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
    // backupPath 非空且文件还不存在时, 先把原始值原子写入该文件, 成功后才 setprop。
    // 文件已经在(上次还原失败留下的)则直接沿用, 不覆盖。写入失败或已有内容不可用时不改属性。
    // 返回 false 表示没有进入接管(属性保持原样)。backupPath 为空则只改内存和属性, 供不落盘的测试使用。
    template <class Ops>
    bool enter(Ops& ops, const char* backupPath = nullptr) {
        if (engaged_) return true;
        savedRunning_ = ops.readRunning();
        backupPath_.clear();
        if (backupPath && backupPath[0] && backupFileExists(backupPath)) {
            std::string raw;
            const std::string existing = readWholeFile(backupPath, raw) ? sanitizeEnable(raw) : std::string();
            // 已有文件一律不覆盖。空的或无效的不能当作原值, 也不许 setprop。
            if (existing.empty()) {
                savedEnable_.clear();
                savedRunning_ = false;
                return false;
            }
            savedEnable_ = existing;
            backupPath_ = backupPath;
        } else {
            savedEnable_ = sanitizeEnable(ops.readEnable());
            if (backupPath && backupPath[0]) {
                if (!writeBackupAtomic(backupPath, savedEnable_)) {
                    savedEnable_.clear();
                    savedRunning_ = false;
                    return false;
                }
                backupPath_ = backupPath;
            }
        }
        ops.setEnable("2");
        ops.start();
        engaged_ = true;
        return true;
    }

    // 读回与记下的原值一致才删除备份。setprop 或读回失败则留着文件, 下次退出或下次启动再试。
    template <class Ops>
    void exit(Ops& ops) {
        if (!engaged_) return;
        ops.setEnable(savedEnable_.c_str());
        if (savedRunning_) ops.start();
        else ops.stop();
        if (!backupPath_.empty() && sanitizeEnable(ops.readEnable()) == savedEnable_) {
            unlink(backupPath_.c_str());
            backupPath_.clear();
        }
        engaged_ = false;
    }

    // 文件不存在: Absent。内容为空或不是纯数字: 不 setprop, 删除文件, 打一条警告, 返回 Discarded。
    // 内容有效: setprop 后读回一致才删除(Restored); 失败则保留文件(Kept), 供下次退出或启动再试。
    template <class Ops, class Log>
    static OifaceBackupResult recoverBackup(Ops& ops, const char* path, Log&& log) {
        std::string raw;
        if (!readWholeFile(path, raw)) return OifaceBackupResult::Absent;
        const std::string value = sanitizeEnable(raw);
        if (value.empty()) {
            unlink(path);
            log.warn(kOifaceBackupInvalidLog);
            return OifaceBackupResult::Discarded;
        }
        ops.setEnable(value.c_str());
        if (sanitizeEnable(ops.readEnable()) != value) return OifaceBackupResult::Kept;
        unlink(path);
        log.info(kOifaceBackupRestoredLog);
        return OifaceBackupResult::Restored;
    }

    template <class Ops>
    static OifaceBackupResult recoverBackup(Ops& ops, const char* path) {
        struct Silent {
            void info(const char*) {}
            void warn(const char*) {}
        };
        return recoverBackup(ops, path, Silent{});
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
    static bool writeBackupAtomic(const char* path, const std::string& digits) {
        if (!path || !path[0]) return false;
        char tmp[512];
        const int n = std::snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, static_cast<long>(getpid()));
        if (n <= 0 || n >= static_cast<int>(sizeof(tmp))) return false;
        const std::string payload = digits + "\n";
        int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0 && errno == ENOENT) {
            std::string dir(path);
            const auto slash = dir.find_last_of('/');
            if (slash != std::string::npos && slash > 0) {
                dir.resize(slash);
                mkdir(dir.c_str(), 0755);
            }
            fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        }
        if (fd < 0) return false;
        size_t off = 0;
        while (off < payload.size()) {
            const ssize_t w = write(fd, payload.data() + off, payload.size() - off);
            if (w > 0) { off += static_cast<size_t>(w); continue; }
            if (w < 0 && errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return false;
        }
        if (fsync(fd) != 0) { close(fd); unlink(tmp); return false; }
        if (close(fd) != 0) { unlink(tmp); return false; }
        if (rename(tmp, path) != 0) { unlink(tmp); return false; }
        return true;
    }

    static bool backupFileExists(const char* path) {
        return path && path[0] && access(path, F_OK) == 0;
    }

    static bool readWholeFile(const char* path, std::string& out) {
        out.clear();
        if (!path || !path[0]) return false;
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        char buf[64];
        while (out.size() < 64) {
            const ssize_t r = read(fd, buf, sizeof(buf));
            if (r > 0) { out.append(buf, buf + r); continue; }
            if (r < 0 && errno == EINTR) continue;
            break;
        }
        close(fd);
        if (out.size() > 32) out.resize(32);
        return true;
    }

    bool engaged_ = false;
    bool savedRunning_ = false;
    std::string savedEnable_;
    std::string backupPath_;
};

} // namespace kin
