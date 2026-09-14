#pragma once
#include "NodeValue.hpp"
#include <cerrno>
#include <string_view>
#include <sys/stat.h>

namespace kin {
enum class NodeWriteCode { Written, Unchanged, Invalid, OpenFailed, ReadFailed, TooLong, WriteFailed, Mismatch };
struct NodeWriteResult {
    NodeWriteCode code;
    int error = 0;
    bool ok() const { return code == NodeWriteCode::Written || code == NodeWriteCode::Unchanged; }
};

// Existing value nodes only: no O_CREAT, O_TRUNC, or writes to invented capabilities.
inline NodeWriteResult writeExistingNode(const char* path, std::string_view value,
                                         bool unlockPermissions = false) noexcept {
    if (!path || !*path || value.empty()) return {NodeWriteCode::Invalid, EINVAL};
    int flags = O_WRONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef _WIN32
    flags |= O_BINARY | O_NOINHERIT;
#endif
    int fd = open(path, flags);
    if (fd < 0 && unlockPermissions && (errno == EACCES || errno == EPERM)) {
        chmod(path, 0666);
        fd = open(path, flags);
    }
    if (fd < 0) return {NodeWriteCode::OpenFailed, errno};
    // A sysfs store is a single command. Never retry a partial command as a suffix.
    ssize_t count;
    do { count = write(fd, value.data(), value.size()); } while (count < 0 && errno == EINTR);
    const int error = count < 0 ? errno : count != static_cast<ssize_t>(value.size()) ? EIO : 0;
    const int closed = close(fd);
    if (error) return {NodeWriteCode::WriteFailed, error};
    if (closed != 0) return {NodeWriteCode::WriteFailed, errno};
    return {NodeWriteCode::Written, 0};
}

inline NodeWriteResult writeChangedNode(const char* path, std::string_view value,
                                        bool unlockPermissions = false, bool verify = false) noexcept {
    if (!path || !*path || value.empty()) return {NodeWriteCode::Invalid, EINVAL};
    auto comparison = compareNodeValue(path, value);
    if (comparison == NodeComparison::Equal) return {NodeWriteCode::Unchanged, 0};
    if (comparison == NodeComparison::Unreadable) return {NodeWriteCode::ReadFailed, errno};
    if (comparison == NodeComparison::TooLong) return {NodeWriteCode::TooLong, EOVERFLOW};
    auto result = writeExistingNode(path, value, unlockPermissions);
    if (!result.ok() || !verify) return result;
    comparison = compareNodeValue(path, value);
    if (comparison == NodeComparison::Equal) return result;
    if (comparison == NodeComparison::Unreadable) return {NodeWriteCode::ReadFailed, errno};
    return {NodeWriteCode::Mismatch, EIO};
}
} // namespace kin
