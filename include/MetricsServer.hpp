// Kin 指标推流服务 (Scene 式 native daemon + 本地 Socket; Phase 2 追加 Metric 式共享内存零拷贝)
//
// 目的: 把"前端 App 每秒 fork 多次 root shell / 逐个 open sysfs"这套开销较大的取数方式,
//       换成"守护常驻、通过一条本地 socket 把已采好的快照持续送给 App"——这正是 Scene 的
//       native daemon + 本地 Socket 架构。
//
// 两级传输(能力协商, 全部可降级):
//   ① 文本(Phase 1, 永远可用, 兜底): 每秒一行 `key=value` 以 '\n' 结尾。旧版 App(v11)只认这个。
//   ② 共享内存零拷贝(Phase 2, Metric 式): 守护用 memfd_create 建一块环形缓冲区, 经 SCM_RIGHTS
//      把 fd 从【同一条 socket】发给 App; App 把 fd map 成只读 ByteBuffer, 之后守护每帧只在 socket
//      上发 8 字节序号(门铃), App 按序号从共享内存直接读 payload —— payload 不再逐字节过 socket。
//
// 版本兼容(关键): App 连上后先发一行 "K1\n" 表示"我支持共享内存"。守护短超时读这个握手:
//   - 读到 "K1" 且 ring 可用 → 走 ② (发 "KIN1 shm=1\n" + fd, 之后发门铃);
//   - 没读到(旧版 App 不发) / ring 不可用 → 走 ① (直接每秒发 key=value 文本行, 与 v11 完全一致)。
//   新版 App 连旧守护: 发了 "K1\n" 但旧守护无视, 只收到文本行 → 自动走 ①。全组合都通。
//
// 健壮: 一切失败(bind/memfd/sendmsg/write)都只影响本推流路径, 绝不影响调度主流程; 都失败时 App
//       侧读不到 socket → 回退自采 sysfs, 行为与接入前一致。
#pragma once

#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <cerrno>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <cctype>
#include <endian.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "Utils.hpp"
#include "Logger.hpp"

// 项目头(Utils.hpp 里 `using namespace LibUtils`)在全局注入了同名定宽整型, 直接写 ku64 会歧义;
// 用带前缀别名规避, 明确指向 std 的定宽整型。
using ku8  = std::uint8_t;
using ku32 = std::uint32_t;
using ku64 = std::uint64_t;

// ── Metric 式共享内存环形缓冲区(单生产者=守护, 多消费者=各 App 只读) ──
// 布局(全 8 字节对齐, 便于 App 端 VarHandle 原子读):
//   头部 HEADER=0x40: [0x0]=magic(u32) [0x4]=version(u32) [0x8]=latestSeq(u64, release 更新)
//   槽 STRIDE=512 × SLOTS=256; slotBase = HEADER + ((seq-1)%256)*STRIDE
//     [+0x00]=seq(u64, release 写, App acquire 读校验==期望) [+0x08]=size(u32) [+0x10]=payload
class RingShm {
public:
    static constexpr ku32 MAGIC   = 0x4B494E31;      // "KIN1"
    static constexpr ku32 VERSION = 1;
    static constexpr size_t   HEADER  = 0x40;
    static constexpr size_t   STRIDE  = 512;
    static constexpr size_t   SLOTS   = 256;
    static constexpr size_t   PAYLOAD_OFF = 0x10;
    static constexpr size_t   PAYLOAD_MAX = STRIDE - PAYLOAD_OFF;   // 496
    static constexpr size_t   SIZE    = HEADER + SLOTS * STRIDE;

    RingShm() { init(); }

    ~RingShm() {
        if (base_) { munmap(base_, SIZE); base_ = nullptr; }
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
    }

    RingShm(const RingShm&) = delete;
    RingShm& operator=(const RingShm&) = delete;

    bool valid() const { return base_ != nullptr && fd_ >= 0; }
    int fd() const { return fd_; }

    // 单生产者调用: 写 payload+size, 再 release 写槽 seq, 最后 release 更新头部 latestSeq。
    ku64 publish(const char* data, size_t n) {
        if (!valid()) return 0;
        if (n > PAYLOAD_MAX) n = PAYLOAD_MAX;
        ku64 seq = ++seq_;
        ku8* slot = base_ + HEADER + ((seq - 1) % SLOTS) * STRIDE;
        std::memcpy(slot + PAYLOAD_OFF, data, n);
        *reinterpret_cast<ku32*>(slot + 0x08) = static_cast<ku32>(n);
        __atomic_store_n(reinterpret_cast<ku64*>(slot + 0x00), seq, __ATOMIC_RELEASE);
        __atomic_store_n(reinterpret_cast<ku64*>(base_ + 0x08), seq, __ATOMIC_RELEASE);
        return seq;
    }

private:
    int fd_ = -1;
    ku8* base_ = nullptr;
    ku64 seq_ = 0;

    void init() {
        // memfd_create 走 syscall(避免低 API 头文件未声明); MFD_CLOEXEC=1, MFD_ALLOW_SEALING=2
        fd_ = static_cast<int>(syscall(SYS_memfd_create, "kin_ring", 1u | 2u));
        if (fd_ < 0) return;
        if (ftruncate(fd_, static_cast<off_t>(SIZE)) != 0) { close(fd_); fd_ = -1; return; }
        // 加封: 禁止增缩(不加 FUTURE_WRITE, 生产者要持续写)。F_ADD_SEALS=1033, SHRINK=2, GROW=4
        fcntl(fd_, 1033, 2 | 4);
        void* p = mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (p == MAP_FAILED) { close(fd_); fd_ = -1; return; }
        base_ = static_cast<ku8*>(p);
        std::memset(base_, 0, HEADER);
        *reinterpret_cast<ku32*>(base_ + 0x00) = MAGIC;
        *reinterpret_cast<ku32*>(base_ + 0x04) = VERSION;
    }
};

class MetricsServer {
public:
    explicit MetricsServer(std::atomic<bool>& running) : run_(running) {}
    ~MetricsServer() { stop(); }

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    void start() {
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true)) return;
        stopping_.store(false, std::memory_order_relaxed);
        stopFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        publisherThread_ = std::thread([this]() { publisherLoop(); });
        acceptThread_ = std::thread([this]() { acceptLoop(); });
    }

    void stop() {
        if (!started_.exchange(false)) return;
        stopping_.store(true, std::memory_order_relaxed);
        if (stopFd_ >= 0) (void)eventfd_write(stopFd_, 1);
        int srv;
        {
            std::lock_guard<std::mutex> lk(serverMx_);
            srv = serverFd_.exchange(-1);
        }
        if (srv >= 0) { shutdown(srv, SHUT_RDWR); close(srv); }
        {
            std::lock_guard<std::mutex> lk(clientMx_);
            for (int fd : clientFds_) shutdown(fd, SHUT_RDWR);
        }
        if (acceptThread_.joinable()) acceptThread_.join();
        if (publisherThread_.joinable()) publisherThread_.join();
        std::vector<std::thread> clients;
        {
            std::lock_guard<std::mutex> lk(clientMx_);
            clients.swap(clientThreads_);
        }
        for (auto& t : clients) if (t.joinable()) t.join();
        if (stopFd_ >= 0) { close(stopFd_); stopFd_ = -1; }
    }

private:
    static constexpr const char* kSockName = "kin_metrics";

    std::atomic<bool>& run_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<int> serverFd_{-1};
    std::mutex serverMx_;
    int stopFd_ = -1;
    std::thread publisherThread_;
    std::thread acceptThread_;
    std::mutex clientMx_;
    std::vector<int> clientFds_;
    std::vector<std::thread> clientThreads_;
    Logger log_;
    Utils utils_;
    RingShm ring_;

    // 生产者写、客户端线程读的"最新快照"(文本 + 序号)。ring 有效时序号来自 ring.publish。
    std::mutex snapMx_;
    std::string snapLine_;
    ku64 snapSeq_ = 0;

    static socklen_t fillAddr(sockaddr_un& addr) {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0';
        size_t n = strlen(kSockName);
        memcpy(addr.sun_path + 1, kSockName, n);
        return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + n);
    }

    // 唯一生产者: 每秒采一帧 → 写 ring(若可用) → 存最新快照供各客户端线程发送。
    void publisherLoop() {
        (void)utils_.getTopAppCached(1);   // 预热前台探测的函数内 static(单线程)
        if (ring_.valid()) log_.Info("MetricsServer: 共享内存环形缓冲已就绪(memfd)");
        long counter = 0;
        while (run_.load(std::memory_order_relaxed) && !stopping_.load(std::memory_order_relaxed)) {
            std::string line = buildSnapshot(counter);
            ku64 seq = ring_.valid() ? ring_.publish(line.data(), line.size())
                                         : static_cast<ku64>(counter + 1);
            {
                std::lock_guard<std::mutex> lk(snapMx_);
                snapLine_ = line;
                snapSeq_ = seq;
            }
            counter++;
            for (int i = 0; i < 10 && run_.load(std::memory_order_relaxed); ++i)
                usleep(100 * 1000);
        }
    }

    void acceptLoop() {
        int srv = socket(AF_UNIX, SOCK_STREAM, 0);
        if (srv < 0) { log_.Info("MetricsServer: socket() 失败, 推流不启用"); return; }
        // 用同一把锁串行化监听 socket 初始化与 stop()，避免 stop() 在 bind/listen 前关闭 fd。
        {
            std::lock_guard<std::mutex> lk(serverMx_);
            if (stopping_.load(std::memory_order_acquire) || !run_.load(std::memory_order_acquire)) {
                close(srv);
                return;
            }
            sockaddr_un addr{};
            socklen_t len = fillAddr(addr);
            if (bind(srv, reinterpret_cast<sockaddr*>(&addr), len) < 0) {
                log_.Info("MetricsServer: bind(@%s) 失败, 推流不启用", kSockName);
                close(srv);
                return;
            }
            if (listen(srv, 4) < 0) {
                log_.Info("MetricsServer: listen 失败");
                close(srv);
                return;
            }
            serverFd_.store(srv, std::memory_order_release);
        }
        log_.Info("MetricsServer: 已在 @%s 上监听, 等待 App 连接", kSockName);
        while (run_.load(std::memory_order_relaxed) && !stopping_.load(std::memory_order_relaxed)) {
            int cli = accept(srv, nullptr, nullptr);
            if (cli < 0) {
                if (stopping_.load(std::memory_order_relaxed) || !run_.load(std::memory_order_relaxed)) break;
                if (errno == EINTR) continue;
                continue;
            }
            std::lock_guard<std::mutex> lk(clientMx_);
            clientFds_.push_back(cli);
            clientThreads_.emplace_back([this, cli]() { serveClient(cli); });
        }
        int expected = srv;
        if (serverFd_.compare_exchange_strong(expected, -1)) close(srv);
    }

    static bool writeAll(int fd, const void* data, size_t size) {
        const char* p = static_cast<const char*>(data);
        while (size > 0) {
            ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
            if (n > 0) { p += n; size -= static_cast<size_t>(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }

    // 短超时读客户端能力握手: 新版 App 连上先发 "K1\n"。读到 'K' 开头即认为支持共享内存。
    bool readClientHello(int cli) {
        struct pollfd pfd { cli, POLLIN, 0 };
        int pr = poll(&pfd, 1, 400);   // 最多等 400ms(旧版 App 不发 → 超时按文本模式)
        if (pr <= 0) return false;
        char buf[8] = {0};
        ssize_t n = recv(cli, buf, sizeof(buf), 0);
        return n > 0 && buf[0] == 'K';
    }

    // 用 sendmsg + SCM_RIGHTS 把 memfd 连同一段普通数据(握手行)一起发给客户端。
    bool sendFd(int cli, const std::string& hello, int fd) {
        struct msghdr msg{};
        struct iovec iov{};
        iov.iov_base = const_cast<char*>(hello.data());
        iov.iov_len  = hello.size();
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char ctrl[CMSG_SPACE(sizeof(int))];
        memset(ctrl, 0, sizeof(ctrl));
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        struct cmsghdr* cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type  = SCM_RIGHTS;
        cm->cmsg_len   = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &fd, sizeof(int));
        ssize_t n;
        do { n = sendmsg(cli, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
        return n == static_cast<ssize_t>(hello.size());
    }

    void serveClient(int cli) {
        bool wantShm = readClientHello(cli);
        bool shm = false;
        if (wantShm && ring_.valid()) {
            // 发能力握手行 + memfd。App 读到 "KIN1 shm=1" 即抓 ancillary fd, 之后只收 8 字节门铃。
            shm = sendFd(cli, std::string("KIN1 shm=1\n"), ring_.fd());
        }
        // 文本模式不发任何握手行, 直接推 key=value —— 与旧版 App(v11)完全一致。
        ku64 lastSeq = 0;
        while (run_.load(std::memory_order_relaxed) && !stopping_.load(std::memory_order_relaxed)) {
            std::string line; ku64 seq;
            { std::lock_guard<std::mutex> lk(snapMx_); line = snapLine_; seq = snapSeq_; }
            if (seq != 0 && seq != lastSeq) {
                lastSeq = seq;
                if (shm) {
                    ku64 le = htole64(seq);
                    if (!writeAll(cli, &le, sizeof(le))) break;   // 8 字节门铃
                } else {
                    std::string nl = line;
                    nl.push_back('\n');
                    if (!writeAll(cli, nl.data(), nl.size())) break;
                }
            }
            for (int i = 0; i < 2 && run_.load(std::memory_order_relaxed); ++i)
                usleep(100 * 1000);   // ~200ms 轮询新帧(生产者 1s 一帧)
        }
        {
            std::lock_guard<std::mutex> lk(clientMx_);
            for (int& fd : clientFds_) {
                if (fd == cli) { fd = -1; break; }
            }
        }
        close(cli);
    }

    // ── 轻量 sysfs 读取 ──
    static std::string readOne(const char* path) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return "";
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return "";
        buf[n] = '\0';
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                         buf[n - 1] == ' '  || buf[n - 1] == '\t')) buf[--n] = '\0';
        return std::string(buf);
    }
    static std::string readFirst(const char* const* candidates) {
        for (const char* const* pp = candidates; *pp; ++pp) {
            std::string v = readOne(*pp);
            if (!v.empty()) return v;
        }
        return "";
    }
    static std::string gpuCurRaw() {
        static const char* const kGpuCur[] = {
            "/sys/class/kgsl/kgsl-3d0/gpuclk",
            "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
            "/sys/kernel/gpu/gpu_clock",
            nullptr,
        };
        return readFirst(kGpuCur);
    }
    static std::string cpuTempRaw() {
        DIR* d = opendir("/sys/class/thermal");
        if (!d) return "";
        std::string best;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
            char tp[128], vp[128];
            snprintf(tp, sizeof(tp), "/sys/class/thermal/%s/type", e->d_name);
            std::string type = readOne(tp);
            if (type.empty()) continue;
            for (auto& c : type) c = (char)tolower((unsigned char)c);
            bool hit = false;
            for (const char* const* kw = Soc::kThermalKeywords; *kw; ++kw)
                if (type.find(*kw) != std::string::npos) { hit = true; break; }
            if (!hit) continue;
            snprintf(vp, sizeof(vp), "/sys/class/thermal/%s/temp", e->d_name);
            std::string t = readOne(vp);
            if (!t.empty()) { best = t; break; }
        }
        closedir(d);
        return best;
    }

    // 组装一行快照(不含末尾 '\n'; 发送/入 ring 时各自处理)。字段读不到即省略。
    std::string buildSnapshot(long seq) {
        std::string s;
        s.reserve(256);
        auto add = [&](const char* k, const std::string& v) {
            if (v.empty()) return;
            std::string clean;
            clean.reserve(v.size());
            for (char c : v) { if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break; clean.push_back(c); }
            if (clean.empty()) return;
            if (!s.empty()) s.push_back(' ');
            s += k; s.push_back('='); s += clean;
        };
        add("seq", std::to_string(seq));
        std::string fg = utils_.getTopAppCached(1500);
        if (!fg.empty() && fg != "null") add("fg", fg);
        add("gpuload", readFirst(Soc::kGpuBusy));
        add("gpumhz",  gpuCurRaw());
        add("temp",    cpuTempRaw());
        add("curr",    readFirst(Soc::kCurrentNow));
        add("volt",    readOne("/sys/class/power_supply/battery/voltage_now"));
        add("cap",     readOne("/sys/class/power_supply/battery/capacity"));
        add("status",  readOne("/sys/class/power_supply/battery/status"));
        DIR* d = opendir("/sys/devices/system/cpu/cpufreq");
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != nullptr) {
                if (strncmp(e->d_name, "policy", 6) != 0) continue;
                const char* num = e->d_name + 6;
                if (!*num) continue;
                char fp[160], gp[160];
                snprintf(fp, sizeof(fp), "/sys/devices/system/cpu/cpufreq/%s/scaling_cur_freq", e->d_name);
                snprintf(gp, sizeof(gp), "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", e->d_name);
                std::string f = readOne(fp);
                std::string g = readOne(gp);
                add((std::string("cpu") + num).c_str(), f);
                if (!g.empty()) add((std::string("gov") + num).c_str(), g);
            }
            closedir(d);
        }
        return s;
    }
};
