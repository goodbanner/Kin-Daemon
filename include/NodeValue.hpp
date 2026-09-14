#pragma once

#include <cerrno>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>

namespace kin {

enum class NodeComparison { Equal, Different, Unreadable, TooLong };

// Compare only readable value nodes; trigger/write-only nodes need a separate writer.
inline NodeComparison compareNodeValue(const char* path, std::string_view wanted) noexcept {
    if (!path) return NodeComparison::Unreadable;
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef _WIN32
    flags |= O_BINARY;
#endif
    const int fd = open(path, flags);
    if (fd < 0) return NodeComparison::Unreadable;
    // One page plus a sentinel byte: equality requires EOF, not a matching prefix.
    constexpr size_t maxValueBytes = 4096;
    char current[maxValueBytes + 1];
    size_t used = 0;
    while (used < sizeof(current)) {
        const auto n = read(fd, current + used, sizeof(current) - used);
        if (n > 0) { used += static_cast<size_t>(n); continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        close(fd);
        return NodeComparison::Unreadable;
    }
    close(fd);
    if (used > maxValueBytes) return NodeComparison::TooLong;
    while (used > 0 && (current[used - 1] == '\n' || current[used - 1] == '\r' ||
                        current[used - 1] == ' ' || current[used - 1] == '\t')) --used;
    return std::string_view(current, used) == wanted
        ? NodeComparison::Equal : NodeComparison::Different;
}

} // namespace kin
