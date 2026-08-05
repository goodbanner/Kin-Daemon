#pragma once 

#include "Utils.hpp"
#include "LibUtils.hpp"

class Logger {
private:
    static constexpr const char* logpath = "/sdcard/Android/Kin/log.txt";

    constexpr static int LINE_SIZE = 1024 * 8;   // 8 KiB，单行日志远小于此，省 3 实例 ×24KB
    char lineCache[LINE_SIZE];

    LOG_LEVEL logLevel_ = LOG_LEVEL::INFO;  // 默认 INFO(对齐 MW_CpuTurboScheduler: 开箱即有日志);
                                            // config meta.loglevel 可改 DEBUG/INFO/WARN/ERROR/OFF
    mutex logPrintMutex;
public:
    void Debug(const char* message) {
        Log(LOG_LEVEL::DEBUG, message);
    } 

    void Info(const char* message) {
        Log(LOG_LEVEL::INFO, message);
    }
    void Warn(const char* message) {
        Log(LOG_LEVEL::WARN, message);
    }

    void Error(const char* message) {
        Log(LOG_LEVEL::ERROR, message);
    }

    template<typename... Args>
    void Debug(const char* message, Args&&... args) {
        Log(LOG_LEVEL::DEBUG, message, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(const char* message, Args&&... args) {
        Log(LOG_LEVEL::INFO, message, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(const char* message, Args&&... args) {
        Log(LOG_LEVEL::WARN, message, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(const char* message, Args&&... args) {
        Log(LOG_LEVEL::ERROR, message, std::forward<Args>(args)...);
    }

    // qlib::string operator== 已修为按内容比较；此处保守保留 strcmp（语义本就正确）
    void setLogLevel(string_t& level) {
        const char* s = level.c_str();
        if      (!strcmp(s, "DEBUG")) logLevel_ = LOG_LEVEL::DEBUG;
        else if (!strcmp(s, "INFO"))  logLevel_ = LOG_LEVEL::INFO;
        else if (!strcmp(s, "WARN") || !strcmp(s, "WARNING")) logLevel_ = LOG_LEVEL::WARN;   // WARNING = MW 写法
        else if (!strcmp(s, "ERROR")) logLevel_ = LOG_LEVEL::ERROR;
        else if (!strcmp(s, "OFF") || !strcmp(s, "SILENT") || !strcmp(s, "NONE"))
            logLevel_ = LOG_LEVEL::OFF;   // 静默:完全不写日志
    }

    void clear_log() {
        // Create parent directory if needed
        mkdir("/sdcard/Android/Kin", 0755);
        mkdir("/sdcard/Android", 0755);
        auto temp = fopen(logpath, "wb");
        if (!temp) {
            fprintf(stderr, "ERROR:清理日志文件失败");
            return;
        }
        fclose(temp);
    }

private:
    // 超限时保留后半段（行对齐），避免日志突然全没
    static constexpr long LOG_MAX_SIZE = 1024 * 512;

    void WriteFile(const char* content, const int len) noexcept {
        // O_CLOEXEC: 本守护进程多处用 popen(fork+exec), 避免日志 fd 泄漏给子进程。
        int fd = open(logpath, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0666);

        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && st.st_size >= LOG_MAX_SIZE) {
                const long keep = LOG_MAX_SIZE / 2;
                char* buf = static_cast<char*>(malloc(keep));
                if (buf) {
                    ssize_t n = pread(fd, buf, keep, st.st_size - keep);
                    if (n > 0) {
                        long off = 0;
                        while (off < n && buf[off] != '\n') off++;
                        if (off < n) off++;
                        if (ftruncate(fd, 0) == 0)
                            write(fd, buf + off, n - off);
                    } else {
                        ftruncate(fd, 0);
                    }
                    free(buf);
                } else {
                    ftruncate(fd, 0);
                }
            }
            write(fd, content, len);
            close(fd);
        }
    }


    int getCurrentTimeStr(char* buf, size_t size) {
        if (size == 0) return 0;
        time_t now = time(nullptr);
        struct tm lt;
        // localtime() 可能返回 NULL(异常 TZ/时间)→ strftime(NULL) 是 UB/崩溃。
        // 用线程安全的 localtime_r 并对失败兜底, 这是每条日志的热路径。
        if (!localtime_r(&now, &lt)) { buf[0] = '\0'; return 0; }
        return strftime(buf, size, "%Y-%m-%d %H:%M:%S ", &lt);
    }

    // 去掉日志正文里所有 () 及其中文字(半角 () 与全角 () 都处理),并吃掉紧邻 '(' 前的一个空格,
    // 避免留下尾随空格。原地处理 [s, s+len),返回新长度。(按用户要求:日志输出不带括号注释。)
    static int stripParens(char* s, int len) {
        int w = 0, depth = 0;
        for (int r = 0; r < len; ) {
            const unsigned char c = (unsigned char)s[r];
            const bool fwOpen  = (r + 2 < len) && c == 0xEF && (unsigned char)s[r+1] == 0xBC && (unsigned char)s[r+2] == 0x88;
            const bool fwClose = (r + 2 < len) && c == 0xEF && (unsigned char)s[r+1] == 0xBC && (unsigned char)s[r+2] == 0x89;
            if (c == '(' || fwOpen) {
                if (depth == 0 && w > 0 && s[w-1] == ' ') w--;   // 吃掉括号前的一个空格
                depth++;
                r += fwOpen ? 3 : 1;
                continue;
            }
            if (c == ')' || fwClose) {
                if (depth > 0) depth--;
                r += fwClose ? 3 : 1;
                continue;
            }
            if (depth == 0) s[w++] = (char)c;
            r += 1;
        }
        return w;
    }

    void Log(LOG_LEVEL level, const char* message) {
        if (level < logLevel_) return;  // 热路径 Debug 被过滤时不抢锁不格式化
        lock_guard<mutex> lock(logPrintMutex);

        {
            int len = getCurrentTimeStr(lineCache, sizeof(lineCache));
            len += FastSnprintf(lineCache + len, sizeof(lineCache) - len, "%s ", levelStr(level));
            const int msgStart = len;
            len += FastSnprintf(lineCache + len, sizeof(lineCache) - len, "%s", message);
            len = msgStart + stripParens(lineCache + msgStart, len - msgStart);   // 去括号注释
            lineCache[len++] = '\n';

            #if DEBUG_DURATION
                printf("%s\n", lineCache);
            #endif
            WriteFile(lineCache, len);
        }
    }

    template<typename... Args>
    void Log(LOG_LEVEL level, const char* message, Args&&... args) {
        if (level < logLevel_) return;
        lock_guard<mutex> lock(logPrintMutex);

        {
            const int prefixLen = getCurrentTimeStr(lineCache, sizeof(lineCache));

            int len = prefixLen + FastSnprintf(lineCache + prefixLen, sizeof(lineCache) - prefixLen, "%s ", levelStr(level));
            const int msgStart = len;

            len += snprintf(lineCache + len, sizeof(lineCache) - len, message, std::forward<Args>(args)...);

            if (len <= prefixLen || (size_t)len >= sizeof(lineCache) - 1) {
                lineCache[prefixLen] = '\0';
                fprintf(stderr, "日志异常: len[%d] lineCache[%s]\n", len, lineCache);
                return;
            }

            len = msgStart + stripParens(lineCache + msgStart, len - msgStart);   // 去括号注释
            lineCache[len++] = '\n';

            #if DEBUG_DURATION
                printf("%s\n", lineCache);
            #endif
            WriteFile(lineCache, len);
        }
    }


    // 等级名:用 switch 直接返回常量字符串。不依赖任何静态对象的动态初始化,
    // 既无"静态初始化顺序"风险(曾导致表未就绪时 .at() 抛 std::out_of_range 崩溃),
    // 也永不抛异常。
    static constexpr const char* levelStr(LOG_LEVEL level) noexcept {
        switch (level) {
            case LOG_LEVEL::DEBUG: return "调试 ->";
            case LOG_LEVEL::INFO:  return "信息 ->";
            case LOG_LEVEL::WARN:  return "警告 ->";
            case LOG_LEVEL::ERROR: return "错误 ->";
            case LOG_LEVEL::OFF:   return "";      // 静默级别不会走到这里(消息已被过滤)
        }
        return "信息 ->";
    }
};