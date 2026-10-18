// 主机测试专用 POSIX 垫片(仅 _WIN32 生效, 生产代码不受影响)。
//
// AutoFsm 经 Logger.hpp → Utils.hpp 间接依赖少量 POSIX 接口, mingw 缺少其中几个符号,
// 导致 FSM 无法在 Windows 主机上单测。这里只补"能编译"所需的最小定义:
// 测试不会真的写日志/写节点, open() 失败即等价于"传感器不可用", 正是 FSM 需要覆盖的分支。
//
// 必须在任何 Kin 头文件之前 include。
#pragma once

#ifdef _WIN32
#  ifndef O_NONBLOCK
#    define O_NONBLOCK 0
#  endif
#  ifndef O_CLOEXEC
#    define O_CLOEXEC 0
#  endif
#  include <direct.h>
#  include <io.h>
#  include <cstdio>
#  include <sys/stat.h>
#  include <ctime>
#  include <cstddef>

// Logger::clear_log() 用两参数 mkdir(path, mode)。
#  define mkdir(path, mode) _mkdir(path)

static inline long long kin_host_pread(int fd, void* buf, unsigned int n, long long off) {
    const long long saved = _lseeki64(fd, 0, SEEK_CUR);
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    const int got = _read(fd, buf, n);
    _lseeki64(fd, saved, SEEK_SET);
    return got;
}
#  define pread kin_host_pread

static inline struct tm* kin_host_localtime_r(const time_t* t, struct tm* out) {
    return localtime_s(out, t) == 0 ? out : nullptr;
}
#  define localtime_r kin_host_localtime_r
#endif
