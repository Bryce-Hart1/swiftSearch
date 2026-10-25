#pragma once
/**
 * The on-disk record format: one Envelope is one record.
 *
 * Layout (all integers fixed-width little-endian; see bytes.hpp for why):
 * @attention current layout of record
 *
 *   off  size  field
 *     0     4  magic         0x1A525353 -> on disk: 53 53 52 1A  ('S' 'S' 'R' 0x1A)
 *     4     1  version       RECORD_VERSION
 *     5     1  compression   Compression; unknown value is rejected
 *     6     1  flags         bit0 Tombstone, bit1 Box, bit2 Manifest
 *     7     1  var_type      VAR_TYPE; unknown value is rejected
 *     8     4  key_len
 *    12     8  payload_len
 *    20     8  timestamp_ns  int64, ns since the Unix epoch
 *    28     4  header_crc    CRC32C over bytes [0, 28)
 *    32   ...  key           key_len bytes
 *         ...  payload       payload_len bytes
 *           4  body_crc      CRC32C over (key ++ payload)
 *
 * Overhead is 36 bytes plus the key.
 *
 * Three decisions worth knowing, all of which deviate from a first reading of
 * section 4 of docs/swift-search-design-notes.md:
 *
 * 1. header_crc sits at offset 28, after the timestamp, not at 20 before it.
 *    The notes' field order leaves the timestamp outside its own header's
 *    checksum. That is not cosmetic: the timestamp sits next to tombstone-versus-
 *    write bookkeeping, so an unchecksummed one is a silent-resurrection bug
 *    waiting to happen. Same 32 bytes, same fields, strictly more coverage.
 *
 * 2. body_crc covers the key AND the payload, not the payload alone. With the
 *    payload alone, the key bytes are protected by nothing -- header_crc covers
 *    the key_len field, never the key's contents -- so one flipped bit in a key
 *    decodes cleanly and returns the right payload under the WRONG key, which no
 *    scrub could ever detect. Chaining the CRC makes closing that hole free.
 *
 * 3. The magic carries no version. "SSW1" would have encoded a format version in
 *    two places that can disagree; the version byte is the single source of
 *    truth. The trailing 0x1A is the PNG/CP-M end-of-file byte, so a segment
 *    opened in text mode or `cat`ed to a terminal gives itself away immediately.
 *
 * 4. Offset 7 holds var_type instead of a reserved byte. A reader has to be able
 *    to tell a client what kind of value it is handing back, and a blob of JPEG
 *    bytes does not say so anywhere. The reserved byte was exactly the right
 *    size and already inside the header CRC's coverage, so this cost no bytes
 *    and the header stayed 32. The "must be 0" check it replaced is not lost:
 *    an unknown var_type is refused the same way an unknown compression id is,
 *    which is the stronger check of the two because it also carries meaning.
 *
 * ORDERING: timestamp_ns is metadata and NEVER an ordering key. "The last record
 * for a key wins" is resolved by replay order -- segment id, then byte offset --
 * because std::chrono::system_clock is not monotonic and NTP steps it backwards.
 * Comparing two records' timestamps to decide which is newer is a correctness
 * bug that reproduces approximately never and then eats a key.
 *
 * ERROR MODEL: decode never throws; every failure is a DecodeError value, because
 * hitting a torn tail is the normal path on every crash recovery, not an
 * exception. encode DOES throw std::length_error, because an oversized key in an
 * Envelope the caller just built in memory is a bug in the caller, not untrusted
 * input. (bstd's own writeCount makes the same call for the same reason.)
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <version>

#include "bytes.hpp"
#include "var_type.hpp"

static_assert(__cpp_lib_expected >= 202211L,
              "swiftSearch needs std::expected (C++23): libc++ 16+ / Apple clang 15+.");

namespace swiftsearch {

// On disk: 53 53 52 1A. Stored through store_u32_le, so this constant is the
// value, and the byte order is bytes.hpp's business.
inline constexpr std::uint32_t RECORD_MAGIC = 0x1A525353u;
inline constexpr std::uint8_t RECORD_VERSION = 1;

inline constexpr std::size_t HEADER_SIZE = 32;
inline constexpr std::size_t TRAILER_SIZE = 4;
inline constexpr std::size_t RECORD_OVERHEAD = HEADER_SIZE + TRAILER_SIZE;  // 36

// Caps, checked before anything is reserved or copied. A corrupt length field
// must not be able to trigger a huge allocation; bstd::store::serialize's
// fitsCount makes the same point one layer down.
inline constexpr std::uint32_t MAX_KEY_LEN = 64u * 1024u;
inline constexpr std::uint64_t MAX_PAYLOAD_LEN = 64ull * 1024ull * 1024ull;  // notes 10: chunk cap

enum class Compression : std::uint8_t {
    None = 0,
    Zstd = 1,  // reserved; rejected until stage 3 implements it
};

enum class Flag : std::uint8_t {
    Tombstone = 1u << 0,  // delete marker; requires payload_len == 0
    Box = 1u << 1,        // this key is a container; payload is box metadata
    Manifest = 1u << 2,   // payload is a chunk list (stage: chunking)
};

// Bits 0-3 are critical: a reader that does not understand one cannot return the
// record correctly, so an unknown critical bit is an error. Bits 4-7 are
// advisory: a reader may ignore one it does not know and still return correct
// bytes. (The gzip FLG / PNG chunk-case trick.) That split is what lets an
// advisory flag be added later with no version bump.
inline constexpr std::uint8_t FLAG_CRITICAL_MASK = 0x0Fu;
inline constexpr std::uint8_t FLAG_ADVISORY_MASK = 0xF0u;
inline constexpr std::uint8_t KNOWN_CRITICAL_FLAGS = 0b0000'0111u;

enum class DecodeStatus : std::uint8_t {
    Ok = 0,
    Truncated,            // fewer bytes present than the record needs -> torn tail
    BadMagic,             // not a record start at all -> resync scan
    HeaderCrcMismatch,    // bit rot in the header; EVERY length field is untrustworthy
    BodyCrcMismatch,      // header sound, key/payload rotten -> report THIS key damaged
    BadVersion,           // intact header, a version this build does not implement
    BadVarType,           // intact header, a value type this build does not know
    BadFlags,             // intact header, unknown critical bit or illegal combination
    BadCompression,       // intact header, unknown compression id
    LengthOverflow,       // length past its cap, or the total wraps size_t
    TombstoneHasPayload,  // intact header, Tombstone set with payload_len != 0
};

struct DecodeError {
    DecodeStatus status = DecodeStatus::Ok;
    std::size_t at = 0;      // byte offset within the input where it was detected
    std::size_t needed = 0;  // Truncated only: bytes the record needs in total
};

/// Truncated. Replay truncates the segment here; this is the expected outcome of
/// a crash mid-write and is NOT a fault to report.
constexpr bool isTornTail(DecodeStatus s) noexcept { return s == DecodeStatus::Truncated; }

/// Not a record at all. Resync scan, or stop.
constexpr bool isNotARecord(DecodeStatus s) noexcept { return s == DecodeStatus::BadMagic; }

/// Real corruption. Report it; never silently discard the data.
constexpr bool isBitRot(DecodeStatus s) noexcept {
    return s == DecodeStatus::HeaderCrcMismatch || s == DecodeStatus::BodyCrcMismatch;
}

/// Intact bytes this build will not interpret. The file is not damaged, so
/// replay must NOT truncate on these.
constexpr bool isPolicy(DecodeStatus s) noexcept {
    switch (s) {
        case DecodeStatus::BadVersion:
        case DecodeStatus::BadVarType:
        case DecodeStatus::BadFlags:
        case DecodeStatus::BadCompression:
        case DecodeStatus::LengthOverflow:
        case DecodeStatus::TombstoneHasPayload:
            return true;
        default:
            return false;
    }
}

/// Stable short text for logs. Returns a view into static storage: no
/// allocation, because a scrub hits this in a loop.
std::string_view describe(DecodeStatus s) noexcept;

struct RecordHeader {
    std::uint8_t version = RECORD_VERSION;
    Compression compression = Compression::None;
    std::uint8_t flags = 0;
    VAR_TYPE var_type = VAR_TYPE::UNSET;
    std::uint32_t key_len = 0;
    std::uint64_t payload_len = 0;
    std::int64_t timestamp_ns = 0;

    bool has(Flag f) const noexcept {
        return (flags & static_cast<std::uint8_t>(f)) != 0;
    }

    /// RECORD_OVERHEAD + key_len + payload_len. Only meaningful once the lengths
    /// have passed the cap check, which is why decode validates before using it.
    std::size_t totalSize() const noexcept {
        return RECORD_OVERHEAD + static_cast<std::size_t>(key_len) +
               static_cast<std::size_t>(payload_len);
    }
};

/// Nanoseconds since the Unix epoch, from system_clock. Metadata only -- see the
/// ORDERING note at the top of this file.
std::int64_t nowNanos() noexcept;

/// One record, in memory.
class Envelope {
public:
    Envelope() = default;

    /// var_type describes the payload, so only a plain value carries one. The
    /// default is BIN: raw bytes of unstated kind.
    static Envelope value(std::string key, Bytes payload, std::int64_t timestamp_ns,
                          VAR_TYPE var_type = VAR_TYPE::BIN);
    static Envelope value(std::string key, Bytes payload, VAR_TYPE var_type = VAR_TYPE::BIN);

    /// Delete marker. The payload is always empty, per section 6 of the notes,
    /// so var_type is UNSET -- the Tombstone bit already says what the payload
    /// is, and a type byte would be a second answer to a settled question.
    static Envelope tombstone(std::string key, std::int64_t timestamp_ns);
    static Envelope tombstone(std::string key);

    /// A container marker. `meta` is the box's OWN metadata, never a child
    /// listing -- membership comes from a prefix scan of the index. See box_meta.
    /// var_type is UNSET for the same reason as a tombstone's: the Box bit
    /// already determines that the payload is a BoxMeta.
    static Envelope box(std::string key, Bytes meta, std::int64_t timestamp_ns);
    static Envelope box(std::string key, Bytes meta);

    /// Appends the whole record to `out`, so a segment writer can build a batch
    /// in one buffer with no intermediate copy.
    /// @throws std::length_error if the key or payload exceeds its cap.
    void encodeTo(Bytes& out) const;
    Bytes encode() const;
    std::size_t encodedSize() const noexcept;

    const std::string& key() const noexcept { return _key; }
    std::span<const std::uint8_t> payload() const noexcept { return _payload; }
    const RecordHeader& header() const noexcept { return _header; }

    bool isTombstone() const noexcept { return _header.has(Flag::Tombstone); }
    bool isBox() const noexcept { return _header.has(Flag::Box); }
    bool isManifest() const noexcept { return _header.has(Flag::Manifest); }

    /// Advisory flag bits, preserved verbatim through a round trip even when this
    /// build does not know what they mean.
    std::uint8_t advisoryFlags() const noexcept { return _header.flags & FLAG_ADVISORY_MASK; }

    /// @internal Assembles an Envelope from already-validated parts. Exists for
    /// decodeRecord; prefer the value/tombstone/box factories everywhere else.
    static Envelope fromParts(RecordHeader header, std::string key, Bytes payload);

private:
    RecordHeader _header{};
    std::string _key;
    Bytes _payload;
};

struct Decoded {
    Envelope envelope;
    std::size_t consumed = 0;  // == envelope.encodedSize(); what the caller advances by
};

/**
 * Decodes the record starting at input[0], ignoring anything after it, so a
 * segment walker can hand over the rest of the file and advance by
 * Decoded::consumed. Never throws.
 *
 * Validation order is the whole design, and it is load-bearing:
 *
 *   1. size < HEADER_SIZE                -> Truncated  (before the magic: a short
 *                                           tail is truncated, not bad-magic)
 *   2. magic mismatch                    -> BadMagic   (before any CRC: if these
 *                                           bytes are not a record, nothing is
 *                                           "corrupt")
 *   3. header_crc over [0,28) mismatch   -> HeaderCrcMismatch, RETURN IMMEDIATELY.
 *      Nothing downstream may be read. A one-bit flip in the version byte must
 *      report bit rot, not BadVersion -- otherwise replay files it as "someone
 *      else's format", skips it, and the damaged key is never reported.
 *   4. version / reserved / flags / compression / tombstone-payload rules
 *   5. length caps and total-size overflow, BEFORE any reserve or copy
 *   6. size < totalSize()                -> Truncated, with a trustworthy `needed`
 *   7. body_crc over (key ++ payload)    -> BodyCrcMismatch
 */
std::expected<Decoded, DecodeError> decodeRecord(std::span<const std::uint8_t> input) noexcept;

/**
 * Steps 1-6 only: validates the header and reports its fields without touching
 * the key or payload and without checking body_crc. This is what a replay walk
 * and a scrub cursor want.
 */
std::expected<RecordHeader, DecodeError> peekHeader(std::span<const std::uint8_t> input) noexcept;

/**
 * Key paths. Keys are '/'-separated paths: "photos/2026/beach.jpg". A Box-flagged
 * record marks a container; its children are the keys under its prefix. These
 * helpers are pure and live here so the path rules have exactly one home from the
 * start, before the index that will depend on them exists.
 *
 * Note the format layer itself does not enforce key policy -- a zero-length key
 * encodes and decodes fine. is_valid is the policy, and SwiftSearch::put is where
 * it will be applied.
 */
namespace keys {

inline constexpr char SEPARATOR = '/';

/// Non-empty, within MAX_KEY_LEN, no leading or trailing '/', no empty segment.
bool is_valid(std::string_view key) noexcept;

/// "a/b/c" -> "a/b"; "a" -> "".
std::string_view parent(std::string_view key) noexcept;

/// "a/b/c" -> "c"; "a" -> "a".
std::string_view leaf(std::string_view key) noexcept;

/// True when `key` is inside `box`, at any depth. Tests the separator boundary,
/// not a bare prefix, so "photos/2026x" is NOT within "photos/2026". A box is
/// not within itself.
bool is_within(std::string_view key, std::string_view box) noexcept;

}  // namespace keys

}  // namespace swiftsearch
