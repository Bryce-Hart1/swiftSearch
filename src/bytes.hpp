#pragma once
/**
 * Fixed-width little-endian byte codec.
 *
 * Every load and store here is shift-based on purpose. Two reasons:
 *
 *   1. The bytes produced are identical on any host byte order, so the on-disk
 *      format is defined by this file rather than by the machine that wrote it.
 *   2. It is safe at any alignment. A record starts at an arbitrary byte offset
 *      inside a segment file, so the header can never be assumed 8-aligned, and
 *      `reinterpret_cast<const std::uint64_t*>` on an unaligned pointer is
 *      undefined behaviour.
 *
 * Because every read goes through shifts, the *internal* alignment of fields in
 * the record header has no effect on correctness or speed. Fields are ordered for
 * checksum coverage and readability, never for alignment.
 *
 * Deliberately NOT used here: `bstd::bit::BitBuffer::from()` / `from_vector()`,
 * which copy in host byte order including struct padding and clear the buffer
 * first, and `bstd::store::convert.hpp::convertToSignedIntegral`, which is dead
 * code. See docs/revival-checklist.md.
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace swiftsearch {

using Bytes = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------
// Raw loads. The caller must already have checked that `p` has enough bytes;
// these are what the record decoder uses after its own length check.
// ---------------------------------------------------------------------------

constexpr std::uint16_t load_u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      static_cast<std::uint16_t>(p[1]) << 8);
}

constexpr std::uint32_t load_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

constexpr std::uint64_t load_u64_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(p[0]) | static_cast<std::uint64_t>(p[1]) << 8 |
           static_cast<std::uint64_t>(p[2]) << 16 | static_cast<std::uint64_t>(p[3]) << 24 |
           static_cast<std::uint64_t>(p[4]) << 32 | static_cast<std::uint64_t>(p[5]) << 40 |
           static_cast<std::uint64_t>(p[6]) << 48 | static_cast<std::uint64_t>(p[7]) << 56;
}

// Unsigned-to-signed conversion is modular and well defined since C++20, so this
// needs no std::bit_cast and stays constexpr.
constexpr std::int64_t load_i64_le(const std::uint8_t* p) noexcept {
    return static_cast<std::int64_t>(load_u64_le(p));
}

// ---------------------------------------------------------------------------
// Raw stores, for patching a fixed-size buffer in place (the header CRC).
// ---------------------------------------------------------------------------

constexpr void store_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

constexpr void store_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

constexpr void store_u64_le(std::uint8_t* p, std::uint64_t v) noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (i * 8));
    }
}

constexpr void store_i64_le(std::uint8_t* p, std::int64_t v) noexcept {
    store_u64_le(p, static_cast<std::uint64_t>(v));
}

// ---------------------------------------------------------------------------
// Append helpers, for building a record into a growing buffer.
// ---------------------------------------------------------------------------

inline void put_u8(Bytes& out, std::uint8_t v) { out.push_back(v); }

inline void put_u16_le(Bytes& out, std::uint16_t v) {
    out.insert(out.end(), 2, 0);
    store_u16_le(out.data() + out.size() - 2, v);
}

inline void put_u32_le(Bytes& out, std::uint32_t v) {
    out.insert(out.end(), 4, 0);
    store_u32_le(out.data() + out.size() - 4, v);
}

inline void put_u64_le(Bytes& out, std::uint64_t v) {
    out.insert(out.end(), 8, 0);
    store_u64_le(out.data() + out.size() - 8, v);
}

inline void put_i64_le(Bytes& out, std::int64_t v) {
    put_u64_le(out, static_cast<std::uint64_t>(v));
}

// ---------------------------------------------------------------------------
// Bounds-checked span reads. Total functions: they return nullopt rather than
// reading out of range, so a corrupt length can never walk off a buffer.
// ---------------------------------------------------------------------------

constexpr bool fits(std::span<const std::uint8_t> in, std::size_t off, std::size_t n) noexcept {
    return off <= in.size() && n <= in.size() - off;
}

constexpr std::optional<std::uint8_t> get_u8(std::span<const std::uint8_t> in,
                                             std::size_t off) noexcept {
    if (!fits(in, off, 1)) return std::nullopt;
    return in[off];
}

constexpr std::optional<std::uint16_t> get_u16_le(std::span<const std::uint8_t> in,
                                                  std::size_t off) noexcept {
    if (!fits(in, off, 2)) return std::nullopt;
    return load_u16_le(in.data() + off);
}

constexpr std::optional<std::uint32_t> get_u32_le(std::span<const std::uint8_t> in,
                                                  std::size_t off) noexcept {
    if (!fits(in, off, 4)) return std::nullopt;
    return load_u32_le(in.data() + off);
}

constexpr std::optional<std::uint64_t> get_u64_le(std::span<const std::uint8_t> in,
                                                  std::size_t off) noexcept {
    if (!fits(in, off, 8)) return std::nullopt;
    return load_u64_le(in.data() + off);
}

}  // namespace swiftsearch
