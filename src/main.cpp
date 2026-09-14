#include "scheduler.hpp"
#include "License.hpp"
#include "Logger.hpp"
#include "MetricsServer.hpp"
#include <csignal>
#include <atomic>
#include <cstdio>
#include <thread>
#include <chrono>
#include <cerrno>
#include <unistd.h>
#include <sys/eventfd.h>
#include <poll.h>

static std::atomic<bool> g_run{true};
// App 下发的付费状态(App 是付费权威, 守护只信任该信号)。仅供守护「知道」是否已付费,
// 不阻断运行 —— 未付费也照常调度; 付费墙在 App 端(功能级)。
static std::atomic<bool> g_paid{false};
// 复查线程的可中断等待句柄: 收到停机信号时唤醒它立即退出(避免每秒空轮询)。
static int g_wakefd = -1;
static void onSignal(int) {
    g_run.store(false, std::memory_order_relaxed);
    // 信号处理里只做异步信号安全操作: eventfd_write 仅是一次 write(2) 系统调用。
    if (g_wakefd >= 0) { (void)eventfd_write(g_wakefd, 1); }
}

// TweetNaCl 的 keypair/sign 路径引用 randombytes;守护只做【验证】,永不调用。
// 仅为满足链接符号而提供兜底(被调到说明逻辑异常,这里不产生可用随机数)。
extern "C" void randombytes(unsigned char* p, unsigned long long n) {
    for (unsigned long long i = 0; i < n; i++) p[i] = 0;
}

// 运行中周期刷新付费状态的间隔:App 激活/续费/退款后, 在此窗口内更新 g_paid(不停机)。
static constexpr int kRecheckSeconds = 5 * 60;

int main(void) {
    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);
    // 指标推流向已断开的 App 客户端 write 会触发 SIGPIPE, 默认会杀掉进程; 忽略之, 让 write 返回 EPIPE。
    signal(SIGPIPE, SIG_IGN);

    Logger licLog;

    // ===== 授权状态(App 驱动: 只读 App 下发的付费信号, 不阻断运行) =====
    // 守护无条件运行; 是否付费由 App 写入 auth.json 告知(App 是付费权威, 功能级付费墙在 App 端)。
    {
        lic::Result r = lic::readAuth();
        g_paid.store(r.paid, std::memory_order_relaxed);
        licLog.Info("授权状态: %s (%s)", r.paid ? "已付费" : "未付费", r.reason.c_str());
    }

    // 复查线程用一次性可中断等待(eventfd + poll)取代每秒空轮询: 30 分钟里只睡 1 次而非醒 1800 次,
    // 是守护唯一可消除的每秒定时唤醒源(过夜深睡省电)。停机信号经 eventfd 立即唤醒它退出。
    g_wakefd = eventfd(0, EFD_CLOEXEC);

    // ===== 运行中周期刷新付费状态(App 激活/续费/退款在窗口内生效; 不停机) =====
    std::thread authThread([&licLog]() {
        bool last = g_paid.load(std::memory_order_relaxed);
        while (g_run.load(std::memory_order_relaxed)) {
            int pr;
            if (g_wakefd >= 0) {
                struct pollfd pfd { g_wakefd, POLLIN, 0 };
                pr = poll(&pfd, 1, kRecheckSeconds * 1000);
            } else {
                pr = 0;
                for (int i = 0; i < kRecheckSeconds && g_run.load(std::memory_order_relaxed); ++i)
                    sleep(1);
                if (g_run.load(std::memory_order_relaxed)) pr = 0;
                else break;
            }
            if (!g_run.load(std::memory_order_relaxed)) break;
            if (pr < 0) continue;          // 被信号打断(EINTR)等 → 重新等待
            if (pr > 0) break;             // 收到停机通知 → 退出刷新线程
            lic::Result r = lic::readAuth();   // pr == 0: 到点刷新 App 付费信号
            g_paid.store(r.paid, std::memory_order_relaxed);
            if (r.paid != last) {          // 仅状态变化时记一行, 避免刷屏
                last = r.paid;
                licLog.Info("授权状态更新: %s (%s)", r.paid ? "已付费" : "未付费", r.reason.c_str());
            }
        }
    });

    // ===== Scene 式指标推流(native daemon + 本地 Socket) =====
    // 守护常驻、已是 root、本就在读这些 sysfs, 由它统一采集再经抽象 unix socket 推给 App,
    // 取代 App 每秒 fork 多次 root shell 的高开销取数。App 侧可降级(读不到 socket 就走原路)。
    MetricsServer metrics(g_run);
    metrics.start();

    // 仅做调度。频率/GPU/电量等遥测与存活判定交给前端 App 自行读取 sysfs / pidof，
    // 守护进程不再写 status.json / daemon.alive。
    Schedule sched;

    while (g_run.load(std::memory_order_relaxed)) {
        if (g_wakefd >= 0) {
            struct pollfd pfd { g_wakefd, POLLIN, 0 };
            int pr = poll(&pfd, 1, -1);
            if (pr < 0 && errno == EINTR) continue;
        } else {
            sleep(1);
        }
    }

    metrics.stop();
    if (g_wakefd >= 0) (void)eventfd_write(g_wakefd, 1);
    if (authThread.joinable()) authThread.join();
    if (g_wakefd >= 0) { close(g_wakefd); g_wakefd = -1; }
    return 0;
}
