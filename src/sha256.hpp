#pragma once
/**
 * SHA-256 (FIPS 180-4), hand-rolled, no dependencies.
 *
 * This is the content-addressing hash: a blob's identity is the hash of its
 * bytes. It must therefore be a function of the bytes alone and stable forever,
 * which is why std::hash is not an option -- std::hash is not required to be
 * stable across runs or implementations, and libc++'s string hash is a seeded
 * MurmurHash with no stability guarantee.
 *
 * The streaming form is the real interface and the one-shot delegates to it.
 * Section 10 of the design notes requires hashing a 20 GB video without holding
 * it in memory, so a chunk-at-a-time update() is a requirement rather than a
 * convenience.
 *
 * Defined in a .cpp for the same ODR reason as crc32c: one definition, one
 * translation unit, no chance of two builds disagreeing.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace swiftsearch {

using Digest = std::array<std::uint8_t, 32>;

/// Lowercase, 64 characters, no prefix. This is the spelling the
/// content-addressed directory layout depends on.
std::string to_hex(const Digest& d);

/// Incremental SHA-256. Feed any number of chunks of any size, then finish().
class Sha256 {
public:
    Sha256() noexcept;

    void update(std::span<const std::uint8_t> data) noexcept;

    /// Single use: pads, appends the length and returns the digest. Calling
    /// update() or finish() again after this is a programmer error.
    Digest finish() noexcept;

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> _h{};
    std::array<std::uint8_t, 64> _buf{};
    std::size_t _buffered = 0;   // bytes currently in _buf
    std::uint64_t _total = 0;    // total bytes fed, for the length suffix
};

/// One-shot convenience.
Digest sha256(std::span<const std::uint8_t> data) noexcept;

}  // namespace swiftsearch
