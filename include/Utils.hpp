#pragma once

#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <string>
#include <vector>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdint>
#include <stdarg.h>
#include <stddef.h>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>
#include "LibUtils.hpp"
#include "Json/string.hpp"
#include "WindowFocus.hpp"
#include "NodeValue.hpp"
#include "ForegroundCache.hpp"
#include "PresenceCache.hpp"
#include "WriteStatus.hpp"

#define DEBUG_DURATION 0
#define MAX_PKG_LEN 128
#define MAX_THREAD_LEN 128
#define CPU_POLICY 8 

using namespace LibUtils;

using string_t = qlib::string_t;

using std::atomic;
using std::stringstream;
using std::unordered_map;
using std::lock_guard;
using std::unique_ptr;
using std::ifstream;
using std::vector;
using std::string;
using std::string_view;
using std::thread;
using std::mutex;
using std::exception;
using std::make_unique;
using std::to_string;
using std::move;

enum class LOG_LEVEL : uint32_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    OFF = 4,      // 静默:高于任何消息级别, Log() 里 level < OFF 恒真 → 全部过滤, 不写 log.txt
};

class Utils {
public:
    void FileWrite(const char* filePath, const char* content) noexcept {
        int fd = open(filePath, O_WRONLY | O_NONBLOCK);

        if (fd < 0) {
            chmod(filePath, 0666);
            fd = open(filePath, O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
        }

        if (fd >= 0) {
            write(fd, content, Faststrlen(content));
            close(fd);
        }
    }

    // sysfs/proc 专用：节点必须预先存在，绝不 chmod 或 O_CREAT。
    // 用于 SysTweaks，避免把缺失能力误当成可创建文件。
    bool FileWriteExisting(const char* filePath, const char* content) noexcept {
        if (!filePath || !content || access(filePath, F_OK) != 0) return false;
        int fd = open(filePath, O_WRONLY | O_CLOEXEC);
        if (fd < 0) return false;
        const size_t len = Faststrlen(content);
        size_t off = 0;
        while (off < len) {
            ssize_t n = write(fd, content + off, len - off);
            if (n > 0) { off += (size_t)n; continue; }
            if (n < 0 && errno == EINTR) continue;
            close(fd);
            return false;
        }
        close(fd);
        return true;
    }


    void FileWrite(const string& filePath, const string& content) noexcept {
        int fd = open(filePath.c_str(), O_WRONLY | O_NONBLOCK);

        if (fd < 0) {
            chmod(filePath.c_str(), 0666);
            fd = open(filePath.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
        }

        if (fd >= 0) {
            write(fd, content.data(), content.size());
            close(fd);
        }
    }


    void FileWrite(const char* filePath, const string_t& content) noexcept {
        int fd = open(filePath, O_WRONLY | O_NONBLOCK);

        if (fd < 0) {
            chmod(filePath, 0666);
            fd = open(filePath, O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
        }

        if (fd >= 0) {
            write(fd, content.data(), content.size());
            close(fd);
        }
    }

    void sleep_ms(const int ms) {
        usleep(1000 * ms);
    }

    // 读当前节点并仅在值变化时写入。sysfs 节点通常不带换行，读取时仅去掉行尾空白。
    // 返回 false 表示节点不存在、读取失败或写入失败；同值跳过返回 true。
    bool FileWriteIfChangedBlocking(const char* filePath, const string_t& content) noexcept {
        if (!filePath) {
            writeStatus_.record(kin::WriteStatus::Failed);
            return false;
        }
        if (content.empty()) {
            writeStatus_.recordNode(filePath, kin::WriteStatus::Failed);
            return false;
        }
        const auto comparison = kin::compareNodeValue(
            filePath, std::string_view(content.data(), content.size()));
        if (comparison == kin::NodeComparison::Equal) {
            writeStatus_.recordNode(filePath, kin::WriteStatus::Unchanged);
            return true;
        }
        // 不以截断或读取失败推断节点值；保留失败语义，交调用方重试/报告。
        if (comparison != kin::NodeComparison::Different) {
            writeStatus_.recordNode(filePath, kin::WriteStatus::Failed);
            return false;
        }
        const bool written = FileWriteBlocking(filePath, content);
        writeStatus_.recordNode(filePath, written ? kin::WriteStatus::Written : kin::WriteStatus::Failed);
        return written;
    }

    bool FileWriteIfChangedBlocking(const char* filePath, const char* content) noexcept {
        return content && FileWriteIfChangedBlocking(filePath, string_t(content));
    }

    void beginWriteBatch() noexcept { writeStatus_.startBatch(); }
    void endWriteBatch() noexcept { writeStatus_.endBatch(); }
    kin::WriteStatus lastWriteStatus() const noexcept { return writeStatus_.get(); }
    kin::WriteStatus writeBatchStatus() const noexcept { return writeStatus_.batch(); }
    // 本批逐节点写入结果(计数 + 失败节点路径), 供状态诊断区分"整批同值跳过"与"部分节点写失败"。
    std::string writeBatchNodeSummary() const { return writeStatus_.batchNodeSummary(); }
    std::string writeBatchFailedNodes() const { return writeStatus_.batchFailedNodes(); }

    // scaling_governor 等节点不能用 O_NONBLOCK（内核返回 EAGAIN 导致静默失败），失败时解锁权限重试
    bool FileWriteBlocking(const char* filePath, const string_t& content) noexcept {
        for (int attempt = 0; attempt < 2; ++attempt) {
            int fd = open(filePath, O_WRONLY);
            if (fd < 0) {
                chmod(filePath, 0666);
                fd = open(filePath, O_WRONLY);
            }
            if (fd >= 0) {
                ssize_t n = write(fd, content.data(), content.size());
                close(fd);
                if (n == (ssize_t)content.size()) return true;
            }
            usleep(20000); // 20ms 后重试
        }
        return false;
    }

    int readInt(const char* path) noexcept {
        auto fd = open(path, O_RDONLY);
        if (fd < 0) return 0;

        char buff[16] = { 0 };
        auto len = read(fd, buff, sizeof(buff));
        close(fd);

        if (len <= 0) return 0;

        buff[15] = 0;
        return atoi(buff);
    }

    // affected_cpus 全簇 offline 时为空，首字符不是数字即无在线核
    bool FileStartsWithDigit(const char* path) noexcept {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return false;
        char buff[8] = { 0 };
        ssize_t len = read(fd, buff, sizeof(buff) - 1);
        close(fd);
        if (len <= 0) return false;
        for (ssize_t i = 0; i < len; i++) {
            char c = buff[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            return (c >= '0' && c <= '9');
        }
        return false;
    }

    void WriteInt(const char* path, int value) noexcept {
        auto fd = open(path, O_WRONLY);
        if (fd < 0) {
            chmod(path, 0666);
            fd = open(path, O_WRONLY);
        }
        if (fd < 0) return;

        char tmp[16];
        auto len = FastSnprintf(tmp, sizeof(tmp), "%d", value);
        write(fd, tmp, len);
        close(fd);
    }

    void popenShell(const char* cmd, char* buf, size_t buf_size) {
        if (!buf || buf_size == 0) return;
        buf[0] = '\0';
        auto fp = popen(cmd, "r");
        if (!fp) return;
        if (fgets(buf, buf_size, fp) == nullptr) buf[0] = '\0';
        pclose(fp);
    }

    // 读 /dev/cpuset/top-app/cgroup.procs + /proc/<pid>/cmdline 取前台包名，无 popen 开销；失败返回空串
    // 焦点应用兜底(fas-rs 同款): dumpsys window 的 mFocusedApp 行 —— 窗口管理器的
    // 权威"当前焦点", AOSP 标准输出, 跨 ROM 通用(fas-rs 全机型依赖它做前台判定)。
    // grep -m1 保证 popen 读回极小。样例:
    //   mFocusedApp=ActivityRecord{... u0 com.tencent.tmgp.dfm/.MainActivity t123}
    // 解析: 取行内第一个含 '/' 的 token, 斜杠前即包名。调用方需限频(一次 ~100-300ms)。
    string getFocusedAppWindow() {
        auto* pipe = popen("dumpsys window 2>/dev/null", "r");
        if (!pipe) return "";
        kin::WindowFocus focus;
        char line[2048];
        while (fgets(line, sizeof(line), pipe)) focus.consume(line);
        pclose(pipe);
        return focus.package();
    }

    string getTopAppFast() {
        // cpuset 读取仍需访问 /proc/<pid>/cmdline；短 TTL 防止 FSM/事件线程
        // 在同一前台切换期间重复扫描。缓存空结果也避免错误路径形成扫描风暴。
        {
            std::lock_guard<std::mutex> lk(topAppFastMtx_);
            if (topAppFastCached_ &&
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - topAppFastAt_).count() < 300)
                return topAppFastValue_;
        }
        // 候选 cpuset 路径(不同 Android 版本/cgroup 布局)。
        // cgroup.procs(进程 PID)优先于 tasks(线程 TID): top-app 下每个 App 有几十~上百线程,
        // 按 tasks 枚举会对同一进程的每个 TID 都 open /proc/<tid>/cmdline —— 每 tick 最大 syscall 源。
        // 按 procs 枚举每进程仅一次, 结果相同(同进程所有线程 cmdline 一致), syscall 数量降一个量级。
        static const char* taskPaths[] = {
            "/dev/cpuset/top-app/cgroup.procs",
            "/dev/cpuset/top-app/tasks",
        };
        for (const char* tp : taskPaths) {
            int fd = open(tp, O_RDONLY);
            if (fd < 0) continue;
            char buf[4096];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n <= 0) continue;
            buf[n] = '\0';

            string best;
            char* save = nullptr;
            for (char* tok = strtok_r(buf, "\n", &save); tok; tok = strtok_r(nullptr, "\n", &save)) {
                int pid = atoi(tok);
                if (pid <= 0) continue;
                char cmdPath[64];
                snprintf(cmdPath, sizeof(cmdPath), "/proc/%d/cmdline", pid);
                int cfd = open(cmdPath, O_RDONLY);
                if (cfd < 0) continue;
                char cmd[256] = { 0 };
                ssize_t cn = read(cfd, cmd, sizeof(cmd) - 1);
                close(cfd);
                if (cn <= 0) continue;
                cmd[cn] = '\0';
                // 去掉 ":xxx" 子进程后缀
                char* colon = strchr(cmd, ':');
                if (colon) *colon = '\0';
                if (strchr(cmd, '.') && !strchr(cmd, '/'))
                    best = cmd;
            }
            if (!best.empty()) {
                std::lock_guard<std::mutex> lk(topAppFastMtx_);
                topAppFastValue_ = best;
                topAppFastAt_ = std::chrono::steady_clock::now();
                topAppFastCached_ = true;
                return best;
            }
        }
        {
            std::lock_guard<std::mutex> lk(topAppFastMtx_);
            topAppFastValue_.clear();
            topAppFastAt_ = std::chrono::steady_clock::now();
            topAppFastCached_ = true;
        }
        return "";
    }

    // 判断包名是否仍在 top-app cpuset：小窗盖住游戏时游戏进程通常仍留在 top-app
    bool isPackageInTopApp(const char* pkg) {
        if (!pkg || !*pkg) return false;
        static const char* taskPaths[] = {
            "/dev/cpuset/top-app/tasks",
            "/dev/cpuset/top-app/cgroup.procs",
        };
        for (const char* tp : taskPaths) {
            int fd = open(tp, O_RDONLY);
            if (fd < 0) continue;
            char buf[4096];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n <= 0) continue;
            buf[n] = '\0';
            char* save = nullptr;
            for (char* tok = strtok_r(buf, "\n", &save); tok; tok = strtok_r(nullptr, "\n", &save)) {
                int pid = atoi(tok);
                if (pid <= 0) continue;
                char cmdPath[64];
                snprintf(cmdPath, sizeof(cmdPath), "/proc/%d/cmdline", pid);
                int cfd = open(cmdPath, O_RDONLY);
                if (cfd < 0) continue;
                char cmd[256] = { 0 };
                ssize_t cn = read(cfd, cmd, sizeof(cmd) - 1);
                close(cfd);
                if (cn <= 0) continue;
                cmd[cn] = '\0';
                char* colon = strchr(cmd, ':');
                if (colon) *colon = '\0';
                if (strcmp(cmd, pkg) == 0) return true;
            }
        }
        return false;
    }

    // 包进程是否还存活：扫 /proc/<pid>/cmdline，主进程或 pkg:子进程任一在即算运行。
    // 用于"游戏档保活到退出"——前台离开但进程未死时维持画像。
    bool isPackageRunning(const char* pkg) {
        if (!pkg || !*pkg) return false;
        const std::string key(pkg);
        return packagePresenceCache_.alive(key, 2000, [](const std::string& name) {
            DIR* d = opendir("/proc");
            if (!d) return false;
            bool found = false;
            struct dirent* e;
            while ((e = readdir(d)) != nullptr) {
                if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
                bool numeric = true;
                for (const char* p = e->d_name; *p; ++p)
                    if (*p < '0' || *p > '9') { numeric = false; break; }
                if (!numeric) continue;
                char cmdPath[64];
                snprintf(cmdPath, sizeof(cmdPath), "/proc/%s/cmdline", e->d_name);
                int cfd = open(cmdPath, O_RDONLY);
                if (cfd < 0) continue;
                char cmd[256] = { 0 };
                ssize_t cn = read(cfd, cmd, sizeof(cmd) - 1);
                close(cfd);
                if (cn <= 0) continue;
                cmd[cn] = '\0';
                char* colon = strchr(cmd, ':');
                if (colon) *colon = '\0';
                if (name == cmd) { found = true; break; }
            }
            closedir(d);
            return found;
        });

    }

    // topPkg = Z 序最顶的可见标准 Task（过滤输入法/通知栏等非 standard Task）
    // visible = 所有 visible=true 的 Task 包名（供 isPackageVisible 判小窗场景）
    using ForegroundSnapshot = kin::ForegroundSnapshot;

private:
    kin::WriteStatusTracker writeStatus_;

    kin::ForegroundCache<> foregroundCache_;
    mutable std::mutex topAppFastMtx_;
    kin::PresenceCache<> packagePresenceCache_;
    std::string topAppFastValue_;
    std::chrono::steady_clock::time_point topAppFastAt_{};
    bool topAppFastCached_ = false;

public:

    ForegroundSnapshot parseForegroundOnce() {
        ForegroundSnapshot s;
        char buf[16384] = { 0 };
        popenRead("dumpsys activity activities 2>/dev/null | grep 'Task{'", buf, sizeof(buf));
        if (!buf[0]) return s;
        s.valid = true;
        char* save = nullptr;
        for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
            char* a = strstr(line, "A=");       // 形如 A=<uid>:<pkg>
            if (!a) continue;
            char* colon = strchr(a, ':');
            if (!colon) continue;
            char pkg[160];
            int k = 0;
            for (char* p = colon + 1; *p && *p != ' ' && *p != '}' && *p != '\t'
                                       && k < (int)sizeof(pkg) - 1; ++p)
                pkg[k++] = *p;
            pkg[k] = '\0';
            if (k == 0) continue;
            if (!strstr(line, "visible=true")) continue;
            s.visible.emplace_back(pkg);
            if (strstr(line, "type=standard")) {
                s.standardVisible.emplace_back(pkg);   // 多窗口/小窗下会有多个, 按 Z 序保留
                if (s.topPkg.empty()) s.topPkg = pkg;
            }
        }
        return s;
    }

    ForegroundSnapshot getForegroundCached(int ttlMs) {
        // 即使 fresh/ttl=0 或查询失败，也至少间隔 2s；并发只允许一次 Activity 探测。
        // 新鲜无障碍推送在 Schedule 中直接返回，不受本缓存限制。
        return foregroundCache_.get(ttlMs, [this] { return parseForegroundOnce(); });
    }

    // ColorOS 小窗报 mode=fullscreen 而非 freeform，故用 visible=true 判游戏是否仍在渲染
    bool isPackageVisible(const char* pkg) {
        if (!pkg || !*pkg) return false;
        ForegroundSnapshot s = getForegroundCached(1200);
        if (!s.valid) return false;
        for (const auto& v : s.visible)
            if (v == pkg) return true;
        return false;
    }


    // mCurrentFocus 作为 getTopApp 回退：支持多用户（u0/u10...），"null" 表示熄屏/锁屏
    string getTopAppFocus() {
        char data[1024] = { 0 };
        popenRead("dumpsys window 2>/dev/null | grep mCurrentFocus", data, sizeof(data));
        if (!data[0]) return "";
        bool sawNull = (strstr(data, "mCurrentFocus=null") != nullptr);

        // 扫描 " u<digits> " 模式，取其后到 '/' 的包名
        for (char* w = data; (w = strstr(w, " u")) != nullptr; ++w) {
            char* p = w + 2;
            if (*p < '0' || *p > '9') continue;
            while (*p >= '0' && *p <= '9') ++p;
            if (*p != ' ') continue;
            ++p;
            char* slash = strchr(p, '/');
            if (!slash || slash <= p) continue;
            ptrdiff_t len = slash - p;
            if (len <= 0 || len >= 256) continue;
            char temp[256];
            memcpy(temp, p, len);
            temp[len] = '\0';
            if (strchr(temp, ' ') || strchr(temp, '{') || strchr(temp, '}'))
                continue;
            return string(temp);
        }
        return sawNull ? string("null") : string("");
    }

    string getTopApp() {
        // 三级回退：① activities 最顶可见标准 Task → ② mCurrentFocus → ③ top-app cpuset
        ForegroundSnapshot s = getForegroundCached(0);
        if (s.valid && !s.topPkg.empty()) return s.topPkg;

        string focus = getTopAppFocus();
        if (focus == "null") return "null";
        if (!focus.empty()) return focus;

        string fast = getTopAppFast();
        if (!fast.empty()) return fast;
        return "";
    }

    // dumpsys 每次约 100~300ms，TTL 缓存减少调用频率
    string getTopAppCached(int minIntervalMs = 1500) {
        using clock = std::chrono::steady_clock;
        static std::mutex   m;
        static std::atomic_flag probing = ATOMIC_FLAG_INIT;
        static std::string  lastResult = "null";
        static clock::time_point lastTime{};
        const auto now = clock::now();
        {
            std::lock_guard<std::mutex> lk(m);
            const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
            if (since < minIntervalMs && !lastResult.empty()) return lastResult;
            // Do not queue behind a slow dumpsys or start a duplicate probe from a
            // concurrent scheduler/event thread. The previous result is safer than
            // multiplying shell processes; an empty result remains an explicit miss.
            if (probing.test_and_set(std::memory_order_acquire)) return lastResult;
        }
        const std::string r = getTopApp();
        {
            std::lock_guard<std::mutex> lk(m);
            lastResult = r;
            lastTime   = clock::now();
        }
        probing.clear(std::memory_order_release);
        return r;
    }

    size_t popenRead(const char* cmd, char* buf, size_t len) {
        if (!buf || len == 0) return 0;
        auto fp = popen(cmd, "r");
        if (!fp) { buf[0] = '\0'; return 0; }
        auto readLen = fread(buf, 1, len - 1, fp);
        pclose(fp);
        buf[readLen] = '\0';
        return readLen;
    }
};
