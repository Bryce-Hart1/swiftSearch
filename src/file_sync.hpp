#pragma once
/**
 * Durable file writes: the two POSIX helpers every on-disk writer in swiftSearch
 * shares (the segment files and the store.meta start-date file).
 *
 * Header-only and free of any store type, so a test can use them without opening a
 * store.
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <unistd.h>

namespace swiftsearch {

/// Push a descriptor's data to stable storage. fsync alone does not flush the
/// drive's cache on macOS, so F_FULLFSYNC is tried first where it exists.
inline bool syncDescriptor(const int fd) noexcept {
#ifdef F_FULLFSYNC
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return true;
    }
#endif
    return ::fsync(fd) == 0;
}

/// Write to a temp file, fsync it, rename it over `file`, then fsync the folder.
/// After a crash `file` holds the old content or the new content, never a mixture.
/// @return false if any step failed; the temp file is removed and `file` is untouched
inline bool writeFileAtomically(const std::filesystem::path& file,
                                std::span<const std::uint8_t> bytes) {
    const std::filesystem::path temp = file.string() + ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    std::size_t done = 0;
    while (ok && done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0) {
            ok = (errno == EINTR);
        } else {
            done += static_cast<std::size_t>(n);
        }
    }
    ok = ok && syncDescriptor(fd);
    ::close(fd);
    if (!ok || ::rename(temp.c_str(), file.c_str()) != 0) {
        ::unlink(temp.c_str());
        return false;
    }
    const int dir = ::open(file.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        (void)::fsync(dir);
        ::close(dir);
    }
    return true;
}

}  // namespace swiftsearch
