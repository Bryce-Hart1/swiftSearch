#pragma once
/**
 * The payload of a Box-flagged record.
 *
 * A box is a container of envelopes, expressed as a key path prefix: the
 * envelopes "inside" `photos/2026` are the keys under `photos/2026/`. Membership
 * therefore lives in the index, found by a prefix scan, and a Box record's
 * payload carries only what the path itself cannot express.
 *
 * Deliberately NOT in here:
 *   - a child listing. Adding one photo would rewrite the whole listing, so a
 *     10k-photo box would rewrite ~400 KB on every insert. An append-only store
 *     cannot do that cheaply, and the index already knows the membership.
 *   - a child count or total size. Both are derivable, and a cached count that
 *     can drift from reality is worse than no count.
 *   - a thumbnail. Section 11 of the notes already makes thumbnails their own
 *     records.
 *
 * Two rules:
 *   - `display_name` is presentation only. The path in the key is the identity.
 *     Looking a box up by display name would create a second identity system.
 *   - `meta_version` is independent of the record's `version`. A payload encoding
 *     must be able to evolve without a format-wide version bump, and vice versa.
 *
 * Layout (same fixed-width little-endian discipline as the record header):
 *
 *   off  size  field
 *     0     1  meta_version       currently 1
 *     1     1  meta_flags         reserved, must be 0
 *     2     2  reserved           must be 0
 *     4     8  created_ns         int64, ns since the Unix epoch
 *    12     2  display_name_len
 *    14     2  tag_count
 *    16   ...  display_name       display_name_len bytes, UTF-8, uninterpreted
 *         ...  tags               tag_count x [u16 len][len bytes]
 */

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"

namespace swiftsearch {

inline constexpr std::uint8_t BOX_META_VERSION = 1;
inline constexpr std::size_t BOX_META_FIXED_SIZE = 16;

/// Keeps a corrupt count from driving a huge allocation, the same discipline the
/// record decoder applies to key_len and payload_len.
inline constexpr std::uint16_t MAX_BOX_TAGS = 1024;
inline constexpr std::uint16_t MAX_BOX_NAME_LEN = 4096;

enum class BoxMetaStatus : std::uint8_t {
    Ok = 0,
    Truncated,       // fewer bytes than the structure needs
    BadVersion,      // a meta_version this build does not implement
    BadReserved,     // meta_flags or the reserved field is non-zero
    CountOverflow,   // tag count or name length past its cap
};

std::string_view describe(BoxMetaStatus s) noexcept;

struct BoxMeta {
    std::int64_t created_ns = 0;
    std::string display_name;          // presentation only; never an identity
    std::vector<std::string> tags;     // uninterpreted UTF-8

    Bytes encode() const;
    static std::expected<BoxMeta, BoxMetaStatus> decode(std::span<const std::uint8_t> in) noexcept;
};

}  // namespace swiftsearch
