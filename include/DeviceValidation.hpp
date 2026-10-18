#pragma once

#include "Config.hpp"
#include "Logger.hpp"
#include "NodeValue.hpp"
#include "Soc.hpp"
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>
#include <string>
#include <unistd.h>

// Read-only/transactional smoke checks for beta devices. The report is deliberately
// free of serial numbers, package names and raw credential/config contents.
class DeviceValidation {
public:
    explicit DeviceValidation(Logger& logger) : logger_(logger) {}

    bool run() {
        const std::string report = buildReport();
        return writeAtomic(kReportPath, report);
    }

    static constexpr const char* reportPath() { return kReportPath; }
    static constexpr const char* requestPath() { return kRequestPath; }

private:
    Logger& logger_;
    static constexpr const char* kReportPath = "/sdcard/Android/Kin/AI/device_validation.txt";
    static constexpr const char* kRequestPath = "/sdcard/Android/Kin/AI/device_validation.request";

    static bool readable(const char* path) { return path && access(path, R_OK) == 0; }

    static std::string readText(const char* path) {
        if (!readable(path)) return {};
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return {};
        char buf[256] = {};
        const ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return {};
        std::string out(buf, static_cast<size_t>(n));
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                                out.back() == ' ' || out.back() == '\t')) out.pop_back();
        return out;
    }

    static std::string nodeState(const char* path) {
        if (!readable(path)) return "missing";
        return readText(path).empty() ? "unreadable" : "readable";
    }

    static bool writeAtomic(const char* path, const std::string& data) {
        const std::string tmp = std::string(path) + ".tmp";
        const size_t slash = tmp.rfind('/');
        if (slash != std::string::npos) mkdir(tmp.substr(0, slash).c_str(), 0755);
        int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return false;
        size_t off = 0;
        while (off < data.size()) {
            const ssize_t n = write(fd, data.data() + off, data.size() - off);
            if (n > 0) { off += static_cast<size_t>(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            close(fd); unlink(tmp.c_str()); return false;
        }
        fsync(fd);
        close(fd);
        if (rename(tmp.c_str(), path) != 0) { unlink(tmp.c_str()); return false; }
        return true;
    }

    std::string buildReport() {
        std::string out;
        out += "schema_version=1\n";
        out += "timestamp=" + std::to_string(static_cast<long long>(time(nullptr))) + "\n";
        out += "kind=beta_device_validation\n";
        out += "daemon_build=" __DATE__ " " __TIME__ "\n";
        out += "cpu_policy_count=" + std::to_string(Config::kClusterCount) + "\n";
        int readableCpu = 0;
        for (int i = 0; i < Config::kClusterCount; ++i) {
            const int policy = Config::Policy::CpuPolicy[i];
            if (policy < 0) continue;
            char path[192];
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%d/cpuinfo_max_freq", policy);
            const bool exists = readable(path);
            if (exists) ++readableCpu;
            out += "cpu_policy" + std::to_string(i) + "=" + (exists ? "readable" : "missing") + "\n";
        }
        out += "cpu_readable_count=" + std::to_string(readableCpu) + "\n";
        out += "thermal_sysfs=" + nodeState("/sys/class/thermal") + "\n";
        out += "battery_level=" + nodeState("/sys/class/power_supply/battery/capacity") + "\n";
        out += "battery_current=" + nodeState("/sys/class/power_supply/battery/current_now") + "\n";
        out += "kgsl_gpu=" + nodeState("/sys/class/kgsl/kgsl-3d0/max_gpuclk") + "\n";
        out += "mali_gpu=" + nodeState("/sys/class/devfreq/48000000.mali/max_freq") + "\n";
        out += "vendor_cap_readable=" + nodeState(Soc::kVendorFreqCap) + "\n";
        out += "mode_file=" + nodeState("/sdcard/Android/Kin/mode.txt") + "\n";
        out += "config_file=" + nodeState("/sdcard/Android/Kin/config.json") + "\n";
        out += "auto_status_file=" + nodeState("/sdcard/Android/Kin/AI/auto_status.txt") + "\n";
        out += "capabilities_file=" + nodeState("/sdcard/Android/Kin/AI/capabilities.txt") + "\n";
        out += "report_policy=anonymous_read_only\n";
        logger_.Info("真机验证: 已生成脱敏设备报告 %s", kReportPath);
        return out;
    }
};
