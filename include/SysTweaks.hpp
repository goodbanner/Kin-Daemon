// Kin 系统级优化(移植 ASB 的非 CPU 调优, 仅保留与 CPU 调度器定位相容的部分)
//
// 移植自 AutoSystemBoost(ASB): profiles/{battery,balanced,performance}.sh(每档 VM/网络/WiFi),
// service.sh(开机停调试服务 + WiFi 后台扫描关闭 + BG_TRIM 后台内存回收)。MIT, © 2026 Dima Krylov。
// 不移植音频/相机的 vendor 分区改写(属另一类模块)。所有功能由 config.json 的 Function 开关控制。
#pragma once

#include "Config.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include <string>
#include <cstdio>
#include <cstring>

class SysTweaks {
public:
    SysTweaks(Logger& lg, Utils& ut) : logger_(lg), utils_(ut) {}

    // ── 每档 VM/网络/WiFi 调优(ASB profiles/*.sh 真值) ──
    struct Tune {
        const char* swappiness; const char* dirtyExpire; const char* dirtyWriteback; const char* vfs;
        const char* tcpRmem;    const char* tcpWmem;     const char* fastopen;        const char* keepidle;
        const char* qdisc;      const char* wifiPm;      const char* txqlen;
        // ── V57「network for gamers」每档网络增强(移植 ASB V57 profiles/*.sh + service.sh apply_net) ──
        // 省电档让连接躺平(thin 关、RTO 120s、缓冲小), 游戏/性能档抢低延迟(thin 开、RTO 8s、缓冲大)。
        const char* tcpThin;    // tcp_thin_linear_timeouts + tcp_thin_dupack: 省电 0 / 均衡·性能 1
        const char* tcpRtoMax;  // tcp_rto_max(ms): 省电躺平 120000 / 均衡 15000 / 游戏低延迟 8000
        const char* rmemMax;    const char* wmemMax;     const char* optmemMax;
        const char* backlog;    const char* budget;      const char* budgetUsecs;   const char* devWeight;
        const char* udpMem;     const char* mtuProbing;  const char* finTimeout;    const char* notsent;
        const char* pacingCa;   const char* pacingSs;    // tcp_pacing_ca_ratio / _ss_ratio(配合 BBR)
    };

    // mode: powersave/balance/performance/fast/auto/smart → 映射到 ASB 三档
    void applyProfileTune(const std::string& mode) {
        if (!Config::SysTune::enable) return;
        const Tune& t = tuneFor(mode);
        // VM
        wsys("/proc/sys/vm/swappiness",                 t.swappiness);
        // swappiness 镜像到 OxygenOS 私有属性(仅当已存在, 防 ROM launcher/启动期覆盖本档设置; V57)
        mirrorSwappinessProps(t.swappiness);
        wsys("/proc/sys/vm/dirty_expire_centisecs",     t.dirtyExpire);
        wsys("/proc/sys/vm/dirty_writeback_centisecs",  t.dirtyWriteback);
        wsys("/proc/sys/vm/vfs_cache_pressure",         t.vfs);
        // 网络/TCP(基础)
        wsys("/proc/sys/net/ipv4/tcp_rmem",             t.tcpRmem);
        wsys("/proc/sys/net/ipv4/tcp_wmem",             t.tcpWmem);
        wsys("/proc/sys/net/ipv4/tcp_fastopen",         t.fastopen);
        wsys("/proc/sys/net/ipv4/tcp_keepalive_time",   t.keepidle);
        // ── 网络增强(V57「network for gamers」)—— 全部 only-if-exists(节点缺失 FileWrite 静默失败), 重启自还原 ──
        // 每档差异化: thin/RTO(游戏低延迟 vs 省电躺平)
        wsys("/proc/sys/net/ipv4/tcp_thin_linear_timeouts", t.tcpThin);
        wsys("/proc/sys/net/ipv4/tcp_thin_dupack",          t.tcpThin);
        wsys("/proc/sys/net/ipv4/tcp_rto_max",              t.tcpRtoMax);
        wsys("/proc/sys/net/ipv4/tcp_notsent_lowat",        t.notsent);
        wsys("/proc/sys/net/ipv4/tcp_mtu_probing",          t.mtuProbing);
        wsys("/proc/sys/net/ipv4/tcp_fin_timeout",          t.finTimeout);
        // keepalive 三元组补齐(现只写 time, 补 intvl/probes)
        wsys("/proc/sys/net/ipv4/tcp_keepalive_intvl",      "75");
        wsys("/proc/sys/net/ipv4/tcp_keepalive_probes",     "9");
        // 全档同值: 恢复/追帧相关(early_retrans/recovery)、空闲不重进慢启动
        wsys("/proc/sys/net/ipv4/tcp_early_retrans",        "3");
        wsys("/proc/sys/net/ipv4/tcp_recovery",             "1");
        wsys("/proc/sys/net/ipv4/tcp_slow_start_after_idle","0");
        // 缓冲上限 + 收包批(net.core)
        wsys("/proc/sys/net/core/rmem_max",                 t.rmemMax);
        wsys("/proc/sys/net/core/wmem_max",                 t.wmemMax);
        wsys("/proc/sys/net/core/optmem_max",               t.optmemMax);
        wsys("/proc/sys/net/core/netdev_max_backlog",       t.backlog);
        wsys("/proc/sys/net/core/netdev_budget",            t.budget);
        wsys("/proc/sys/net/core/netdev_budget_usecs",      t.budgetUsecs);
        wsys("/proc/sys/net/core/dev_weight",               t.devWeight);
        wsys("/proc/sys/net/core/somaxconn",                "512");
        wsys("/proc/sys/net/ipv4/udp_mem",                  t.udpMem);
        wsys("/proc/sys/net/core/default_qdisc",            t.qdisc);   // 补写 qdisc(此前 Tune.qdisc 声明未用)
        // 拥塞控制 bbr(退 cubic)+ pacing 比例
        applyCongestion();
        wsys("/proc/sys/net/ipv4/tcp_pacing_ca_ratio",      t.pacingCa);
        wsys("/proc/sys/net/ipv4/tcp_pacing_ss_ratio",      t.pacingSs);
        // tcp_mem 动态按本机 RAM(low 3% / pressure 6% / high 10% of 页数)
        applyTcpMem();
        // WiFi 省电 + 发送队列
        applyWifi(t.wifiPm, t.txqlen);
        logger_.Debug("系统调优[%s]: swappiness=%s tcp_rmem=%s wifi_pm=%s txqlen=%s",
                      mode.c_str(), t.swappiness, t.tcpRmem, t.wifiPm, t.txqlen);
    }

    // ── 开机一次性: 停调试/遥测服务 + 关 WiFi 后台扫描(ASB service.sh)。注意 pm disable 跨重启持久。 ──
    void bootTrimOnce() {
        if (!Config::BootTrim::enable) return;
        // ASB _BG_TRIM_DISABLE: 纯分析/日志类, 禁用不影响功能
        static const char* kDisable[] = {
            "com.oplus.midas", "com.oplus.olc", "com.oplus.crashbox", "com.oplus.logkit"
        };
        for (const char* p : kDisable) sh(std::string("pm disable-user --user 0 ") + p);
        sh("stop vendor.oplus.hardware.olc2-V3-service");
        sh("settings put global wifi_scan_always_enabled 0");
        sh("settings put global wifi_wakeup_enabled 0");
        logger_.Info("开机精简: 已禁用 4 个调试/遥测服务并关闭 WiFi 后台扫描(pm disable 跨重启持久)");
    }

    // ── BG_TRIM: 周期性回收后台内存(ASB asb_bg_trim_*)。绝不杀进程/不动前台, 只 send-trim-memory。 ──
    // topApp: 当前前台包(跳过不 trim); screenOff: 息屏才回收"工作集"应用。
    void bgTrimTick(const std::string& topApp, bool screenOff) {
        if (!Config::BgTrim::enable) return;
        // 重度社交/流媒体: 常驻后台时软回收(BACKGROUND)
        static const char* kHeavy[] = {
            "com.facebook.katana", "com.instagram.android", "com.snapchat.android",
            "com.zhiliaoapp.musically", "com.ss.android.ugc.trill",
            "com.netflix.mediaclient", "com.amazon.mShop.android.shopping", "com.aliexpress.buyer"
        };
        for (const char* p : kHeavy) trimPkg(p, topApp, "BACKGROUND");
        if (screenOff) {
            // 息屏才回收的"最近工作集"(相册/音乐/修图): 更彻底(COMPLETE)
            static const char* kWorkset[] = {
                "com.android.gallery3d", "com.coloros.gallery3d", "com.oneplus.gallery",
                "com.google.android.apps.photos", "com.spotify.music",
                "com.aspiro.tidal", "com.deezer.android.app", "com.google.android.youtube.music"
            };
            for (const char* p : kWorkset) trimPkg(p, topApp, "COMPLETE");
        }
    }

private:
    Logger& logger_;
    Utils&  utils_;

    void wsys(const char* path, const char* val) {
        if (!val || !*val) return;
        // 普通 sysctl/sysfs 参数统一采用读后比较：同值不写，外部模块改过后
        // 下一次应用仍会重新核对真实节点值。FileWriteIfChangedBlocking 不创建
        // 缺失节点，保留 FileWriteExisting 的能力边界。
        if (!utils_.FileWriteIfChangedBlocking(path, string_t(val))) {
            // 缺失节点和只读节点都按单项能力不可用处理，不影响其它调优项。
            logger_.Debug("系统调优节点不可用: %s", path ? path : "(null)");
        }
    }
    void sh(const std::string& cmd) {
        char b[8];
        utils_.popenRead((cmd + " >/dev/null 2>&1").c_str(), b, sizeof(b));
    }
    void applyWifi(const char* pm, const char* txqlen) {
        if (pm && strcmp(pm, "on") == 0) {
            sh("iw dev wlan0 set power_save on");
            wsys("/sys/module/wlan/parameters/wlan_pm", "1");
        } else if (pm && strcmp(pm, "off") == 0) {
            sh("iw dev wlan0 set power_save off");
            wsys("/sys/module/wlan/parameters/wlan_pm", "0");
        }
        // "auto": 不强制, 交回系统默认
        if (txqlen && *txqlen) {
            wsys("/sys/class/net/wlan0/tx_queue_len", txqlen);
            sh(std::string("ip link set wlan0 txqueuelen ") + txqlen);
        }
    }
    // 前台跳过; 对该包所有 pid 发 trim(am send-trim-memory, 软回收不杀进程)
    void trimPkg(const char* pkg, const std::string& topApp, const char* level) {
        if (!pkg || !*pkg) return;
        if (topApp == pkg) return;   // 绝不动前台
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "for _p in $(pidof %s 2>/dev/null); do "
                 "am send-trim-memory --user 0 \"$_p\" %s >/dev/null 2>&1; done",
                 pkg, level);
        char b[8];
        utils_.popenRead(cmd, b, sizeof(b));
    }

    // ── 拥塞控制: 有 bbr 用 bbr, 否则退 cubic(移植 ASB service.sh 可用性探测) ──
    void applyCongestion() {
        char avail[160]; avail[0] = '\0';
        readFirstLine("/proc/sys/net/ipv4/tcp_available_congestion_control", avail, sizeof(avail));
        if (strstr(avail, "bbr"))        wsys("/proc/sys/net/ipv4/tcp_congestion_control", "bbr");
        else if (strstr(avail, "cubic")) wsys("/proc/sys/net/ipv4/tcp_congestion_control", "cubic");
    }
    // ── tcp_mem 动态按本机 RAM(移植 ASB: 页数=MemTotal_kB/4; low 3% / pressure 6% / high 10%) ──
    void applyTcpMem() {
        long kb = readMemTotalKb();
        if (kb <= 0) return;
        long pg = kb / 4;                 // 4KB 页
        long low = pg * 3 / 100, pressure = pg * 6 / 100, high = pg / 10;
        char buf[64];
        snprintf(buf, sizeof(buf), "%ld %ld %ld", low, pressure, high);
        wsys("/proc/sys/net/ipv4/tcp_mem", buf);
    }
    // ── swappiness 镜像 OxygenOS 私有属性(仅当属性已存在才 resetprop, 不新建; 移植 ASB V57 service.sh) ──
    void mirrorSwappinessProps(const char* val) {
        if (!val || !*val) return;
        static const char* kProps[] = {
            "sys.sysctl.swappiness", "sys.mem.swappiness_on_launcher", "sys.mem.swappiness_on_start"
        };
        for (const char* p : kProps) {
            char cur[48]; cur[0] = '\0';
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "getprop %s", p);
            utils_.popenRead(cmd, cur, sizeof(cur));
            bool exists = false;
            for (const char* c = cur; *c; ++c)
                if (*c != '\n' && *c != '\r' && *c != ' ' && *c != '\t') { exists = true; break; }
            if (!exists) continue;         // 属性不存在 → 不新建(同 ASB 语义)
            snprintf(cmd, sizeof(cmd), "resetprop %s %s", p, val);
            sh(cmd);
        }
    }
    long readMemTotalKb() {
        FILE* f = fopen("/proc/meminfo", "r");
        if (!f) return 0;
        char line[128]; long kb = 0;
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break;
        fclose(f);
        return kb;
    }
    static void readFirstLine(const char* path, char* buf, size_t len) {
        buf[0] = '\0';
        FILE* f = fopen(path, "r");
        if (!f) return;
        if (!fgets(buf, (int)len, f)) buf[0] = '\0';
        fclose(f);
    }

    const Tune& tuneFor(const std::string& mode) {
        // ASB battery ── 追加字段: thin,rto,rmemMax,wmemMax,optmem,backlog,budget,budget_us,dev_weight,udp_mem,mtu,fin,notsent,pacingCa,pacingSs
        static const Tune kBattery = {
            "80", "60000", "30000", "120",
            "4096 32768 2097152", "4096 32768 2097152", "1", "18000",
            "fq_codel", "on", "32",
            "0", "120000", "2097152", "2097152", "16384",
            "128", "32", "1500", "8",
            "16384 32768 65536", "0", "30", "16384", "80", "110"
        };
        // ASB balanced
        static const Tune kBalanced = {
            "35", "6000", "4000", "80",
            "4096 262144 16777216", "4096 262144 16777216", "3", "3600",
            "fq_codel", "auto", "1024",
            "1", "15000", "16777216", "16777216", "1048576",
            "2000", "180", "5000", "64",
            "16384 32768 65536", "1", "20", "32768", "110", "170"
        };
        // ASB performance
        static const Tune kPerf = {
            "12", "1000", "800", "30",
            "4096 524288 16777216", "4096 524288 16777216", "3", "600",
            "fq", "off", "3072",
            "1", "8000", "16777216", "16777216", "1048576",
            "4096", "480", "1200", "160",
            "32768 65536 131072", "1", "10", "24576", "160", "240"
        };
        if (mode == "powersave") return kBattery;
        if (mode == "performance" || mode == "fast") return kPerf;
        return kBalanced;   // balance / auto / smart / 其它 → 均衡
    }
};
