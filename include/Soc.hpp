// Kin SoC 适配层 (HAL)
//
// 把所有"机型 / 平台相关"的 sysfs 路径与参考常量集中到这一个文件 —— 适配新平台
// (MTK / 其它骁龙)只需在这里增改候选项, 不必动 AutoMode / AutoFsm / AutoFas 里的算法逻辑。
//
// 设计约定:
//   - 路径一律给"候选列表"(nullptr 结尾), 运行时逐个探测、命中即用 —— 所以为不同平台多写
//     几个候选路径是安全的(读不到自动跳过)。新增 MTK 节点直接往对应数组里加即可。
//   - 默认值面向高通骁龙(8 Elite Gen5 / SM8650 系)。
//   - 遍历惯例: for (const char* const* pp = Soc::kXxx; *pp; ++pp) { const char* p = *pp; ... }
#pragma once

namespace Soc {

    // ── CPU 频率参考(asb_bounds_scale 缩放基准; 运行时再按本机 cpuinfo_max_freq 缩放) ──
    inline constexpr int REF_HWMAX_LITTLE = 2035200;   // 小核簇参考最大频(kHz, SM8650)
    inline constexpr int REF_HWMAX_BIG    = 3302400;   // 大核簇参考最大频(kHz, SM8650)

    // ── 帧源: measured_fps(高通 SDE CRTC 为主; MTK 可在此加对应节点) ──
    inline constexpr const char* const kMeasuredFps[] = {
        "/sys/class/drm/sde-crtc-0/measured_fps",
        "/sys/class/graphics/fb0/measured_fps",
        "/sys/class/drm/sde-crtc-1/measured_fps",
        "/sys/class/drm/card0-DSI-1/measured_fps",
        nullptr,
    };

    // ── GPU 占用% ──
    inline constexpr const char* const kGpuBusy[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/devfreq/gpu_load",
        nullptr,
    };

    // ── 电池放电电流(绝对值, 单位运行时自适配 µA/mA) ──
    inline constexpr const char* const kCurrentNow[] = {
        "/sys/class/power_supply/battery/current_now",
        "/sys/class/power_supply/bms/current_now",
        nullptr,
    };

    // ── 屏幕背光(判亮/灭) ──
    inline constexpr const char* const kScreenBacklight[] = {
        "/sys/class/backlight/panel0-backlight/brightness",
        "/sys/class/leds/lcd-backlight/brightness",
        "/sys/class/backlight/backlight/brightness",
        nullptr,
    };

    // ── 厂商强制频率上限(headroom / anti-clamp 用); 高通 msm_performance ──
    // 单一节点(非候选列表); 不可用时 headroom 信号自动旁路。MTK 暂无等价节点。
    inline constexpr const char* kVendorFreqCap =
        "/sys/kernel/msm_performance/parameters/cpu_max_freq";

    // ── CPU 温度传感器类型关键字(匹配 /sys/class/thermal/*/type, 小写) ──
    inline constexpr const char* const kThermalKeywords[] = {
        "cpu", "apc", "silver", "gold", "prime", "cpuss", nullptr,
    };

    // ── 皮肤/外壳温度传感器优先级(忠实 ASB v59 asb_metrics.h 的五级梯) ──
    // 返回 >0 表示是皮肤类传感器, 数值越小越优先(择一最佳物理外壳 zone); 0=非皮肤。
    // 关键: 物理外壳 shell_*(prio 1-3)优先于功率模型算出的【虚拟】皮肤 skin-virt/skin-msm(prio 4);
    // 虚拟皮肤常跟随结温、不可信, 仅在无物理外壳时兜底。不与 CPU 结温关键字(上)重叠。
    inline int skinThermalPriority(const char* type_lc) {
        if (!type_lc || !*type_lc) return 0;
        if (strcmp(type_lc, "shell_frame") == 0) return 1;
        if (strcmp(type_lc, "shell_front") == 0 || strcmp(type_lc, "shell_back") == 0) return 2;
        if (strstr(type_lc, "shell_")) return 3;
        if (strstr(type_lc, "skin-virt") || strstr(type_lc, "skin-msm")) return 4;
        if (strcmp(type_lc, "skin") == 0 || strstr(type_lc, "back-therm")) return 5;
        return 0;
    }

}  // namespace Soc
