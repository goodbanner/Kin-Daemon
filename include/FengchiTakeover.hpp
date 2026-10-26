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
    // backupPath 非空时, 先把原始值原子写入该文件, 成功后才 setprop。写入失败则不改属性。
    // 返回 false 表示没有进入接管(属性保持原样)。backupPath 为空则只改内存和属性, 供不落盘的测试使用。
    template <class Ops>
    bool enter(Ops& ops, const char* backupPath = nullptr) {
        if (engaged_) return true;
        savedEnable_ = sanitizeEnable(ops.readEnable());
        savedRunning_ = ops.readRunning();
        backupPath_.clear();
        if (backupPath && backupPath[0]) {
            if (!writeBackupAtomic(backupPath, savedEnable_)) {
                savedEnable_.clear();
                savedRunning_ = false;
                return false;
            }
            backupPath_ = backupPath;
        }
        ops.setEnable("2");
        ops.start();
        engaged_ = true;
        return true;
    }

    template <class Ops>
    void exit(Ops& ops) {
        if (!engaged_) return;
        ops.setEnable(savedEnable_.c_str());
        if (savedRunning_) ops.start();
        else ops.stop();
        if (!backupPath_.empty()) {
            unlink(backupPath_.c_str());
            backupPath_.clear();
        }
        engaged_ = false;
    }

    // 上次进程在接管中退出, 备份文件还在。先按文件里的原始值 setprop, 读回一致后再删除。
    // 读回不一致则留着文件, 下次启动再试。文件不存在返回 false。
    template <class Ops>
    static bool recoverBackup(Ops& ops, const char* path) {
        std::string raw;
        if (!readWholeFile(path, raw)) return false;
        const std::string value = sanitizeEnable(raw);
        ops.setEnable(value.c_str());
        if (sanitizeEnable(ops.readEnable()) != value) return false;
        unlink(path);
        return true;
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
