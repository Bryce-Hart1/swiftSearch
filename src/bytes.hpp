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

#include "bstd/bit/bitBuffer.hpp"

namespace swiftsearch {

/**
 * A byte string. It is a std::vector<std::uint8_t> in every respect (all of
 * vector's constructors and members are inherited), plus a way to build one from
 * a BitBuffer. It is a class rather than an alias only because an alias cannot
 * take a constructor.
 */
class Bytes : public std::vector<std::uint8_t> {
public:
    using std::vector<std::uint8_t>::vector;

    Bytes() = default;

    /// Implicit on purpose, so a plain std::vector<std::uint8_t> still converts.
    Bytes(std::vector<std::uint8_t> raw) : std::vector<std::uint8_t>(std::move(raw)) {}

    /**
     * The bytes of a BitBuffer.
     *
     * BitBuffer packs bits MSB-first (its first bit is 0x80 of byte 0), and that
     * is exactly the layout it keeps in memory, so this is a copy of its storage
     * and nothing is reordered. It reads rawBytes() only, not BitBuffer::from(),
     * which is the host-byte-order path the note above rules out.
     *
     * LOSSY when bits.size() is not a multiple of 8: the last byte is padded with
     * 0 bits at the low end and the bit count is NOT carried over. Bytes cannot
     * say "5 bits", so a caller that needs the count back must store it
     * separately (see bits.paddingBits()). An empty buffer gives an empty Bytes.
     */
    explicit Bytes(const bstd::bit::BitBuffer& bits)
        : std::vector<std::uint8_t>(bits.rawBytes()) {}
};

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
