// Kin 自动模式 FSM (仅 mode=auto 启用)
//
// 移植自 AutoSystemBoost(ASB) 的有限状态机, 已对照真实源码 asb_fsm.h / asb_fsm_bounds.generated.h:
//   上游: https://github.com/krilikd/AutoSystemBoost   协议: MIT — Copyright (c) 2026 Dima Krylov
//
// 一致部分: 六状态(DEEP_IDLE/LIGHT_IDLE/MODERATE/HEAVY/SUSTAINED/GAMING)、状态级别 level[]、
// 目标状态判定【结合 CPU/GPU 综合, 非单指标抬高即升】(GPU≥90→GAMING(连续6确认); CPU≥80 或
// GPU≥80且CPU≥50→HEAVY; CPU≥60 或 GPU≥80→MODERATE; 否则 LIGHT_IDLE(日用); 息屏→DEEP_IDLE)、
// CPU 阈值非对称迟滞(进80/出70, 进60/出45)+ CPU% 输入平滑(瞬升缓降, util_est 思路)、
// 升/降迟滞(up_window=2 / down_window=5, 热入 SUSTAINED window=1)、
// 最小驻留(HEAVY 8s / GAMING 25s / SUSTAINED 24s)、SUSTAINED 温控进出与 180s 时间逃逸、
// 频率界按 lerp(floor, ceil, level) 插值, 并由 Smart alpha 在 battery↔balanced 包络间混合。
// 频率界常量取自 asb_fsm_bounds.generated.h(SM8850/骁龙8 Elite Gen5; 小核/大核)。
//
// 适配 Kin: 只在 auto 模式接管 CPU 频率上下限(GPU 仍交基础档/守护), 且【App 画像优先时 FSM 让位】;
// 其余模式(powersave/balance/performance/fast)完全不受影响。MIT→GPL-3.0 兼容, 保留上游版权声明。
#pragma once

#include "Config.hpp"
#include "Logger.hpp"
#include "Soc.hpp"
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

class AutoFsm {
public:
    enum State { DEEP_IDLE = 0, LIGHT_IDLE, MODERATE, HEAVY, SUSTAINED, GAMING, COUNT };
    struct Caps { int minL, maxL, minB, maxB; int state; };

    explicit AutoFsm(Logger& lg) : logger_(lg) {}

    // 喂入本 tick 指标, 推进状态; 返回是否发生状态切换。gpu/temp 为 -1 表示未知。
    // gameHint=true: 前台是已知游戏 App → 直接判为 GAMING(不只看 GPU)。
    bool update(int gpuPct, float load1, int tempC, int screenOn, int cpuBusy = -1, bool gameHint = false,
                int currentMa = -1, bool charging = false) {
        if (!hwProbed_) probeHwmax();
        const State prev = state_;
        const long now = nowSec();

        // 会话起点惰性初始化: 守护在【开屏状态下】启动/开关(初始态 LIGHT_IDLE, 不走息屏→亮屏沿)时,
        // 若不在此补一枪, 首个会话的预热宽限会因 sessionStartSec_==0 而整局失效。
        if (screenOn != 0 && sessionStartSec_ == 0) sessionStartSec_ = now;

        if (screenOn == 0 && state_ != DEEP_IDLE) {
            enter(DEEP_IDLE, now); gamingStreak_ = 0; cpuEst_ = -1; return logFlip(prev);
        }
        if (screenOn != 0 && state_ == DEEP_IDLE) {
            sessionStartSec_ = now;   // 新亮屏会话起点(供 SUSTAINED 预热宽限)
            // 前台已知游戏(亮屏瞬间就开游戏): 直接进 GAMING, 不绕 MODERATE 多等一拍。
            if (gameHint) { gamingStreak_ = GAMING_CONFIRM; enter(GAMING, now); return logFlip(prev); }
            enter(MODERATE, now); return logFlip(prev);
        }

        // util_est 思路的输入平滑: CPU 活跃% 瞬升立即跟随、缓降取半衰 —— 抑制单 tick 尖峰/凹陷造成
        // 的状态抖动(对应内核 PELT util_est = max(均值, 最近值) 与 schedutil 升快降慢)。读不到(-1)透传。
        int cpuS = cpuBusy;
        if (cpuBusy >= 0) {
            cpuEst_ = (cpuEst_ < 0 || cpuBusy >= cpuEst_) ? cpuBusy : (cpuEst_ + cpuBusy) / 2;
            cpuS = cpuEst_;
        }
        // 预热宽限(移植 ASB balanced_warmup_grace): 亮屏会话开头 grace 秒内抑制进 SUSTAINED 热降档,
        // 避免加载发热尖峰把人卡一下; 真烫(≥bypass_temp)则旁路照常降, 不牺牲热安全。
        const int  graceS = Config::AutoMode::fsm_warmup_grace_s;
        const long sessAge = now - sessionStartSec_;   // 用 wall-clock; 回拨(age<0)按宽限结束处理(安全方向)
        const bool warmupActive = (graceS > 0 && sessionStartSec_ > 0 &&
                                   sessAge >= 0 && sessAge < graceS &&
                                   !(tempC >= 0 && tempC >= Config::AutoMode::fsm_warmup_bypass_temp));
        bool thermalToSustained = false;
        const State d = desired(gpuPct, load1, screenOn, tempC, cpuS, gameHint,
                                currentMa, charging, warmupActive, thermalToSustained);

        // 前台已知游戏 → 即时进入 GAMING(兑现 desired()「直接判 GAMING」的设计承诺), 跳过升档窗口确认。
        // 仅作用于上行进入 GAMING(gameHint 是用户配置的显式信号, 稳定不抖); 退出 GAMING 仍走
        // 正常降档迟滞 + 最小驻留(DWELL_GAMING=25s), 不会闪进闪出。
        if (gameHint && d == GAMING && state_ != GAMING) {
            enter(GAMING, now); return logFlip(prev);
        }

        if (d != pending_) { pending_ = d; pendingTicks_ = 0; }
        else               { pendingTicks_++; }

        int window = (d > state_) ? UP_WINDOW : DOWN_WINDOW;
        // 热入 SUSTAINED 改用正常升档窗口(2 tick≈4s)确认, 不再单 tick 即跳 ——
        // 防性能核瞬时升温 + 一次负载尖峰就闪进/闪出「持续高负载」。

        if (pendingTicks_ >= window && d != state_) {
            bool canLeave = true;
            // SUSTAINED 热保持仅约束【降档】(d<state_): 温度仍高于退出阈值则不下行, 防热抖动。
            // 升档(尤其 → GAMING)绝不被此守卫挡住, 否则高温下开游戏会被卡在 SUSTAINED 降频(与
            // 「游戏不被热限降级」的设计相悖)。原实现漏判方向, 上行也被挡 → 游戏卡 0.80 档/GPU 80%。
            if (state_ == SUSTAINED && d < state_ && tempC >= 0 && tempC >= SUSTAINED_EXIT())
                canLeave = false;
            // 时间逃逸: 卡 SUSTAINED ≥180s 无条件放行(防长期卡死)。原先附带温度前置(≤enter-3)会在
            // 温度稳定于 exit..enter-3 死区(如 48–51℃)时既不满足退出守卫又不满足逃逸 → 永久卡死。
            if (!canLeave && state_ == SUSTAINED && (now - enterSec_) >= 180)
                canLeave = true;
            // 降档需满足最小驻留(ASB fsm_min_dwell)
            if (canLeave && d < state_) {
                int dwell = minDwell(state_);
                if (dwell > 0 && (now - enterSec_) < dwell) canLeave = false;
            }
            if (canLeave) { enter(d, now); }
        }
        return logFlip(prev);
    }

    // 由当前状态 + Smart alpha(0..1000 省电倾向)计算各簇频率上下限(kHz)。
    // 频率界常量按 SM8650 参考授权, 运行时按本机 cpuinfo_max_freq 缩放(移植 ASB asb_bounds_scale),
    // 故无需写死某型号 —— 读频率表自适配每颗硅片。
    Caps computeCaps(int alpha) const {
        // 频率端护栏: |alpha|>~2000 后 scale() 已饱和在硬件边界, 更大的值只会带来
        // blend 整型溢出风险(|alpha|≳91.7 万时反转语义), 故换算前钳到 ±10 万(不影响学习值本身)。
        if (alpha >  100000) alpha =  100000;
        if (alpha < -100000) alpha = -100000;
        const float t = LEVEL[state_];

        // ── V62 新思路: cpu_max 缩放按状态级别 t 加权 ──
        // 原来 scale() 对所有状态一视同仁地用 hwmax/ref 比例缩放, 导致 LIGHT_IDLE 的上限
        // 在 SM8850(hwmax=3628800)上从 1190400 被抬到 ~2122505 —— 手机什么都没做却被允许跑到
        // 2.1GHz。V62 修正: 缩放权重乘以状态级别 t, 空闲保持参考值, 游戏才享受硬件全量。
        // SUSTAINED 例外: 它是热降频状态, 级别 0.62 高于 MODERATE(0.45), 但此时恰恰要压频,
        // 不能因为"级别高"就获得更大的缩放——权重上限钳到 MODERATE(0.45f)。
        // cpu_min floor 不做 hwmax/ref 缩放: 空闲地板不随峰值线性增长
        // (SM8850 最低频 ~364MHz ≈ SM8650 ~300MHz, 而峰值相差 1.78x)。改为仅 clamp 到本机真实最低频。
        Caps o;
        o.maxL = scaleWeighted(0, lerp(blend(BAL_F_MAXL, BAT_F_MAXL, alpha), blend(BAL_C_MAXL, BAT_C_MAXL, alpha), t), t);
        o.minL = clampFloor(0, lerp(blend(BAL_F_MINL, BAT_F_MINL, alpha), blend(BAL_C_MINL, BAT_C_MINL, alpha), t));
        o.maxB = scaleWeighted(1, lerp(blend(BAL_F_MAXB, BAT_F_MAXB, alpha), blend(BAL_C_MAXB, BAT_C_MAXB, alpha), t), t);
        o.minB = clampFloor(1, lerp(blend(BAL_F_MINB, BAT_F_MINB, alpha), blend(BAL_C_MINB, BAT_C_MINB, alpha), t));
        if (o.minL > o.maxL) o.minL = o.maxL;
        if (o.minB > o.maxB) o.minB = o.maxB;
        o.state = state_;
        return o;
    }

    const char* stateName() const {
        static const char* N[COUNT] = { "DEEP_IDLE", "LIGHT_IDLE", "MODERATE", "HEAVY", "SUSTAINED", "GAMING" };
        return (state_ >= 0 && state_ < COUNT) ? N[state_] : "?";
    }
    int state() const { return state_; }
    // 本机各簇 cpuinfo_max_freq(kHz, 0=未探到); 供按 App 类别钳制上限(× pct/100)用。
    int hwMaxL() const { return hwmaxL_; }
    int hwMaxB() const { return hwmaxB_; }
    // 本机各簇 cpuinfo_min_freq(kHz); 读不到回退 300MHz(与 scale() 的地板一致)。
    // 供 fsm_policy 百分比换算后钳地板: scaling_max 低于硬件最低频的写入会被内核拒绝/收紧。
    int hwMinL() const { return hwminL_ > 0 ? hwminL_ : 300000; }
    int hwMinB() const { return hwminB_ > 0 ? hwminB_ : 300000; }

    // 每状态 GPU 上限百分比(对齐 ASB 每状态 GPU 包络;空闲低、游戏满)。0=不改(由调用方决定)。
    static int gpuPctForState(int st) {
        switch (st) {
            case DEEP_IDLE:  return 40;   // 息屏: 压低 GPU 上限省电(不残留上一档的高值)
            case LIGHT_IDLE: return 45;   // ASB 把电池档 GPU 地板抬到 45% 防滚动卡顿
            case MODERATE:   return 60;
            case HEAVY:      return 85;
            case SUSTAINED:  return 80;
            case GAMING:     return 100;
            default:         return 0;
        }
    }

private:
    Logger& logger_;
    State state_   = LIGHT_IDLE;
    State pending_ = LIGHT_IDLE;
    int   pendingTicks_ = 0;
    long  enterSec_     = 0;
    int   gamingStreak_ = 0;
    int   cpuEst_       = -1;   // CPU 活跃% 平滑值(util_est 思路: 瞬升缓降); -1=未初始化
    long  sessionStartSec_ = 0; // 亮屏会话起点(息屏→亮屏沿置位; 供 SUSTAINED 预热宽限)

    // 频率自适配: 本机各簇最高/最低频(kHz), 0=未读到(则缩放/地板回退)
    bool  hwProbed_ = false;
    int   hwmaxL_   = 0;   // 小核簇(cluster0) cpuinfo_max_freq
    int   hwmaxB_   = 0;   // 大核簇(cluster1)
    int   hwminL_   = 0;   // 小核簇 cpuinfo_min_freq
    int   hwminB_   = 0;   // 大核簇
    // ASB 参考最高频(SM8650): 频率界常量即按此授权
    // 机型参考频率集中到 SoC 适配层(include/Soc.hpp); 此处保留别名, 调用处不变。
    static constexpr int REF_HWMAX_LITTLE = Soc::REF_HWMAX_LITTLE;
    static constexpr int REF_HWMAX_BIG    = Soc::REF_HWMAX_BIG;

    // ── 阈值/时序(ASB 真值, 经本机高刷调校) ──
    // 高刷屏(144/165Hz)光滑动/合成 GPU 就常到 50–60%, 故 GPU 阈值整体上调, 否则正常用机被误判重载。
    static constexpr int   GAMING_GPU_ENTER = 90;   // ≥90% 才算游戏级 GPU(高刷下区分游戏与重图形)
    static constexpr int   GAMING_GPU_EXIT  = 78;   // 滞回退出
    static constexpr int   GAMING_CONFIRM   = 6;
    static constexpr int   HEAVY_GPU_ENTER  = 80;   // ≥80% 才算 GPU 重负载(用户要求 80% 以上)
    static constexpr int   HEAVY_GPU_EXIT   = 65;   // 进 HEAVY 后 GPU 跌破 65% 才离开(滞回)
    static constexpr float HEAVY_LOAD       = 22.0f;   // 兜底: 读不到 CPU% 时退回 loadavg
    static constexpr float MOD_LOAD         = 16.0f;
    // 主判据: 真实 CPU 活跃%(/proc/stat, 已排除 idle+iowait,即排除 I/O 等待)。8 核均摊。
    // CPU 活跃% 阈值, 带非对称迟滞(进高/出低, 学内核 WALT up~80/down~60)。研究依据: 主线内核三处
    // 一致的 0.8 容量拐点 —— EAS overutilized(fits_capacity cap×1280<max×1024)、schedutil(C=1.25 在
    // util/max=0.8 即满频)、uclamp 顶桶起点 820/1024。故 80% 定为"高负载"入口(不再用 90); "中负载"
    // 下压到比例区中段 60%(80% 已是高载线, 不该当中载)。
    static constexpr int   CPU_HEAVY_ENTER  = 80;      // ≥80% → 重负载
    static constexpr int   CPU_HEAVY_EXIT   = 70;      // 进 HEAVY 后跌破 70% 才离开(迟滞防抖)
    static constexpr int   CPU_MOD_ENTER    = 60;      // ≥60% → 中负载
    static constexpr int   CPU_MOD_EXIT     = 45;      // 进中载后跌破 45% 才离开(迟滞防抖)
    static constexpr int   CPU_COMBO        = 50;      // 结合判定地板: GPU 到重载阈值 + CPU≥此值 才升重载
    // 电流佐证(移植 ASB fsm_desired): 放电时, 负载高还得电流跟上才算真重载, 否则只算中载。
    // 充电 / 电流读不到时旁路(ASB !ma_valid)。电流也能单独把空闲抬到中载(真在耗电)。
    static constexpr int   CUR_HEAVY_MA     = 150;     // 进 HEAVY 的放电电流闸门(mA)
    static constexpr int   CUR_MOD_MA       = 120;     // ≥120mA 单独判中载
    static constexpr int   UP_WINDOW        = 2;   // ×tick(2s)=4s 升档
    static constexpr int   DOWN_WINDOW      = 5;   // ×tick(2s)=10s 降档
    // 热限进/出温度改读 config(Function.AutoMode.fsm_sustained_enter/exit), 供 App 调节;
    // 默认 54/48(ASB V54 热经济档, 原 57/49; 见 Config.hpp:152-153)。解析时已保证 enter>exit。
    static int SUSTAINED_ENTER() { return Config::AutoMode::fsm_sustained_enter; }
    static int SUSTAINED_EXIT()  { return Config::AutoMode::fsm_sustained_exit; }
    static constexpr int   DWELL_HEAVY      = 8;
    static constexpr int   DWELL_GAMING     = 25;
    static constexpr int   DWELL_SUSTAINED  = 24;

    // 状态级别(频率界 lerp 系数, 越高频越高)。SUSTAINED 是【热降档】, 必须低于 HEAVY(0.72), 否则
    // 过热升 SUSTAINED 反而抬高频率(火上浇油)。取 0.62(介于 MODERATE 0.45 与 HEAVY 0.72 之间)真正压频。
    static constexpr float LEVEL[COUNT] = { 0.0f, 0.15f, 0.45f, 0.72f, 0.62f, 1.0f };

    // ── 频率界 kHz(asb_fsm_bounds.generated.h: BAT=battery, BAL=balanced; F=floor, C=ceil; L=小核, B=大核) ──
    // 对齐上游 ASB V57 重生成的 asb_fsm_bounds.generated.h(逐行核对: 12 个界中 9 个与旧值一致,
    // V57 仅下调了 3 个 CEIL 上包络 —— battery 小核 1804800→1670400、battery 大核 2208000→1632000、
    // balanced 小核 3302400→2611200; floor 与其余 CEIL 全部不变, 参考系一致)。方向= 省电/更早降频,
    // 契合本项目"省电优先/能降频"定位; 仍按本机 cpuinfo_max_freq 运行时缩放, 不写死型号。
    static constexpr int BAT_F_MAXL = 921600,  BAT_C_MAXL = 1670400, BAT_F_MINL = 307200, BAT_C_MINL = 921600;
    static constexpr int BAT_F_MAXB = 921600,  BAT_C_MAXB = 1632000, BAT_F_MINB = 614400, BAT_C_MINB = 921600;
    static constexpr int BAL_F_MAXL = 1190400, BAL_C_MAXL = 2611200, BAL_F_MINL = 787200, BAL_C_MINL = 1190400;
    static constexpr int BAL_F_MAXB = 1881600, BAL_C_MAXB = 3974400, BAL_F_MINB = 883200, BAL_C_MINB = 1881600;

    static long nowSec() { return (long)time(nullptr); }

    // 读本机各簇 cpuinfo_max_freq(kHz); policy 号取自 Kin 配置 Policy::CpuPolicy[0]/[1]
    void probeHwmax() {
        hwProbed_ = true;
        hwmaxL_ = readPolicyFreq(Config::Policy::CpuPolicy[0], "cpuinfo_max_freq");
        hwmaxB_ = readPolicyFreq(Config::Policy::CpuPolicy[1], "cpuinfo_max_freq");
        hwminL_ = readPolicyFreq(Config::Policy::CpuPolicy[0], "cpuinfo_min_freq");
        hwminB_ = readPolicyFreq(Config::Policy::CpuPolicy[1], "cpuinfo_min_freq");
        logger_.Info("智能档: 频率自适配 小核[%d-%d]MHz 大核[%d-%d]MHz (读本机真实频率表)",
                     hwminL_ / 1000, hwmaxL_ / 1000, hwminB_ / 1000, hwmaxB_ / 1000);
    }
    static int readPolicyFreq(int policy, const char* node) {
        if (policy < 0) return 0;
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/%s", policy, node);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 0;
        char b[32];
        ssize_t n = read(fd, b, sizeof(b) - 1);
        close(fd);
        if (n <= 0) return 0;
        b[n] = '\0';
        long v = strtol(b, nullptr, 10);
        return v > 0 ? (int)v : 0;
    }
    // 按本机硅片缩放(移植 ASB asb_bounds_scale): scaled = kHz × hwmax / 参考; 钳到 [本机最低频, hwmax]。
    // 地板用真实 cpuinfo_min_freq(读不到回退 300MHz); 无 hwmax 则原样返回, 与 ASB 安全回退一致。
    int scale(int slot, int kHz) const {
        // alpha 不设界后 blend 外插可能得到非正频率: 必须落到簇地板, 绝不能把负值/0
        // 透传出去写 scaling_max(内核拒写 → 上限冻结 + 每 tick 重试风暴)。
        // ">1000 = 更省"的语义由此优雅饱和到硬件最低频。
        if (kHz <= 0) {
            int floorK = (slot == 0) ? hwminL_ : hwminB_;
            return floorK > 0 ? floorK : 300000;
        }
        int hw  = (slot == 0) ? hwmaxL_ : hwmaxB_;
        int ref = (slot == 0) ? REF_HWMAX_LITTLE : REF_HWMAX_BIG;
        if (hw <= 0 || ref <= 0) return kHz;
        std::int64_t scaled = static_cast<std::int64_t>(kHz) * hw / ref;
        int floor = (slot == 0) ? hwminL_ : hwminB_;
        if (floor <= 0) floor = 300000;
        if (scaled > hw)    scaled = hw;
        if (scaled < floor) scaled = floor;
        return (int)scaled;
    }

    // V62: cpu_max 缩放按状态级别 t 加权 —— 空闲保持参考值, 只有高性能状态才享受硬件全量。
    // SUSTAINED 权重上限钳到 MODERATE(0.45f): 热降频状态不因级别较高而获得更大缩放空间。
    int scaleWeighted(int slot, int kHz, float t) const {
        if (kHz <= 0) {
            int floorK = (slot == 0) ? hwminL_ : hwminB_;
            return floorK > 0 ? floorK : 300000;
        }
        int hw  = (slot == 0) ? hwmaxL_ : hwmaxB_;
        int ref = (slot == 0) ? REF_HWMAX_LITTLE : REF_HWMAX_BIG;
        if (hw <= 0 || ref <= 0) return kHz;
        std::int64_t scaled = static_cast<std::int64_t>(kHz) * hw / ref;
        int floor = (slot == 0) ? hwminL_ : hwminB_;
        if (floor <= 0) floor = 300000;
        if (scaled > hw) scaled = hw;
        if (scaled < floor) scaled = floor;
        // 加权混合: raw + (scaled - raw) × w
        // w = t, SUSTAINED 时上限 0.45f(MODERATE 级别), 防热降频状态反而抬高上限
        float w = t;
        if (state_ == SUSTAINED && w > 0.45f) w = 0.45f;
        int result = kHz + (int)((scaled - kHz) * w);
        if (result > hw)    result = hw;
        if (result < floor) result = floor;
        return result;
    }

    // V62: cpu_min floor 不做 hwmax/ref 缩放 —— 空闲地板不随峰值线性增长。
    // 仅 clamp 到本机真实最低频即可(设备最低频接近, 而峰值差距可达 1.78x)。
    int clampFloor(int slot, int kHz) const {
        int floor = (slot == 0) ? hwminL_ : hwminB_;
        if (floor <= 0) floor = 300000;
        int hw = (slot == 0) ? hwmaxL_ : hwmaxB_;
        if (kHz < floor) return floor;
        if (hw > 0 && kHz > hw) return hw;
        return kHz;
    }

    void enter(State s, long now) {
        state_ = s; pending_ = s; pendingTicks_ = 0; enterSec_ = now;
    }
    bool logFlip(State prev) {
        if (state_ == prev) return false;
        // INFO 级的状态切换行由 scheduler 统一打(带前台+类型, 格式一致); 这里只留 Debug 痕迹
        logger_.Debug("FSM=%s", stateName());
        return true;
    }
    static int minDwell(State s) {
        if (s == HEAVY)     return DWELL_HEAVY;
        if (s == GAMING)    return DWELL_GAMING;
        if (s == SUSTAINED) return DWELL_SUSTAINED;
        return 0;
    }
    // alpha=1000→battery(省电), alpha=0→balanced; 越界外插, 全程 long 运算防溢出反转。
    static int blend(int bal, int bat, int alpha) {
        long v = (long)bal + (long)(bat - bal) * alpha / 1000;
        if (v >  2000000000L) v =  2000000000L;
        if (v < -2000000000L) v = -2000000000L;
        return (int)v;
    }
    static int lerp(int a, int b, float t) { return (int)(a + (b - a) * t + 0.5f); }

    State desired(int gpu, float load1, int screenOn, int tempC, int cpuBusy, bool gameHint,
                  int currentMa, bool charging, bool warmupActive, bool& thermalToSustained) {
        thermalToSustained = false;
        if (screenOn == 0) return DEEP_IDLE;

        // 前台是已知游戏 App: 直接判 GAMING(置位连胜即时确认), 不依赖 GPU 阈值。
        if (gameHint) {
            if (gamingStreak_ < GAMING_CONFIRM) gamingStreak_ = GAMING_CONFIRM;
            return GAMING;   // 不被下方热限降级; 过热由 alpha 压频(高温→alpha 升→包络下移)
        }

        if (gpu >= GAMING_GPU_ENTER) { if (gamingStreak_ < 100000) gamingStreak_++; }
        else if (gpu >= 0 && gpu < GAMING_GPU_EXIT) { gamingStreak_ = 0; }

        // HEAVY 滞回: 已在 HEAVY/上层时用退出阈值, 否则用进入阈值(ASB heavy_gpu_exit)
        const int heavyGpuTh = (state_ >= HEAVY) ? HEAVY_GPU_EXIT : HEAVY_GPU_ENTER;
        // CPU 负载判定【排除 I/O】: 优先用真实 CPU 活跃%(已排除 idle+iowait); 读不到才回退原始
        // loadavg(安卓 loadavg 含 I/O 等待会虚高, 仅作兜底)。
        const bool cpuKnown  = cpuBusy >= 0;
        // CPU 阈值非对称迟滞: 已在该层及以上时用退出阈值, 否则用进入阈值(同 GPU 的 heavyGpuTh 写法)。
        const int  cpuHeavyTh = (state_ >= HEAVY)    ? CPU_HEAVY_EXIT : CPU_HEAVY_ENTER;
        const int  cpuModTh   = (state_ >= MODERATE) ? CPU_MOD_EXIT   : CPU_MOD_ENTER;
        const bool cpuHeavy  = cpuKnown ? (cpuBusy >= cpuHeavyTh) : (load1 >= HEAVY_LOAD);
        const bool cpuMod    = cpuKnown ? (cpuBusy >= cpuModTh)   : (load1 >= MOD_LOAD);
        const bool cpuSome   = cpuKnown ? (cpuBusy >= CPU_COMBO)  : (load1 >= MOD_LOAD);
        const bool gpuHeavy  = (gpu >= heavyGpuTh);   // GPU 到重载阈值(进80/退65)
        // 电流佐证(ASB ma_valid): 仅放电且电流可读时启用闸门; 充电/读不到则旁路。
        const bool maValid   = (currentMa > 0 && !charging);
        State d;
        // 【结合 CPU/GPU 综合判定, 不再"单一指标被抬高就升状态"】:
        //  - GAMING : GPU 持续极高(≥90), 或前台已知游戏(上方已直接返回)。
        //  - HEAVY  : CPU 真吃满(≥80 单指标可成立, 出迟滞 70), 或 GPU 重载(≥80)且 CPU 也至少有量(≥50)。
        //             仅 GPU 高而 CPU 闲(如静止 3D 合成)不进重载 —— 只升到中载。放电再要电流佐证。
        //  - MODERATE: CPU 中载(≥60, 出迟滞 45), 或 GPU 单独到重载阈值(≥80, 高但 CPU 不高),
        //             或(读不到 CPU% 时)电流单独佐证。
        //  ※ 已去掉"屏亮+GPU≥30→中载"的 UI 突发规则 —— 高刷屏日用(滚动/视频)GPU 常驻 50–60%,
        //    那条会把日常误判成中载; 日用应停在低负载, 触控由内核 input boost 自然提频。
        if (gpu >= GAMING_GPU_ENTER)
            d = (gamingStreak_ >= GAMING_CONFIRM) ? GAMING : HEAVY;
        else if (cpuHeavy || (gpuHeavy && cpuSome))
            d = (!maValid || currentMa >= CUR_HEAVY_MA) ? HEAVY : MODERATE;
        else if (cpuMod || gpuHeavy
                 || (!cpuKnown && maValid && currentMa >= CUR_MOD_MA))
            // 电流单独佐证仅作【CPU% 读不到时的兜底】: 真实 CPU 活跃% 才是权威, 否则
            // 高基础放电(亮屏 ~400mA)会把轻载误判成中负载。
            d = MODERATE;
        else
            d = LIGHT_IDLE;

        // 热限: 升温到阈值把【持续重载】压到 SUSTAINED(级别 0.80)。GAMING 不在此降级
        // (游戏保持游戏态, 过热由 alpha 压频处理), 否则进游戏总被识别成"持续高负载"。
        if (tempC >= 0 && tempC >= SUSTAINED_ENTER() && d == HEAVY && !warmupActive) {
            d = SUSTAINED;
            thermalToSustained = true;
        }
        return d;
    }
};
