#include "record.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>

#include "crc32c.hpp"

namespace swiftsearch {
namespace {

// ---------------------------------------------------------------------------
// The layout lives here, exactly once, in one translation unit.
//
// This table is the mechanism that designs out the bug the old
// Envelope/EnvelopeUnseal pair had: two functions that each independently
// believed they knew where the fields were, and disagreed. Co-locating an
// encoder and a decoder does not prevent that -- they can still each hardcode
// `20`. Having the offsets exist once *in the source text* does, and the
// static_assert chain below turns a gap, an overlap or a wrong total into a
// compile error rather than a corrupt file.
// ---------------------------------------------------------------------------

struct Field {
    std::size_t off;
    std::size_t len;
};

inline constexpr Field F_MAGIC{0, 4};
inline constexpr Field F_VERSION{4, 1};
inline constexpr Field F_COMPRESSION{5, 1};
inline constexpr Field F_FLAGS{6, 1};
inline constexpr Field F_RESERVED{7, 1};
inline constexpr Field F_KEY_LEN{8, 4};
inline constexpr Field F_PAYLOAD_LEN{12, 8};
inline constexpr Field F_TIMESTAMP{20, 8};
inline constexpr Field F_HEADER_CRC{28, 4};

// Gapless, in order, and exactly HEADER_SIZE bytes.
static_assert(F_MAGIC.off == 0);
static_assert(F_MAGIC.off + F_MAGIC.len == F_VERSION.off);
static_assert(F_VERSION.off + F_VERSION.len == F_COMPRESSION.off);
static_assert(F_COMPRESSION.off + F_COMPRESSION.len == F_FLAGS.off);
static_assert(F_FLAGS.off + F_FLAGS.len == F_RESERVED.off);
static_assert(F_RESERVED.off + F_RESERVED.len == F_KEY_LEN.off);
static_assert(F_KEY_LEN.off + F_KEY_LEN.len == F_PAYLOAD_LEN.off);
static_assert(F_PAYLOAD_LEN.off + F_PAYLOAD_LEN.len == F_TIMESTAMP.off);
static_assert(F_TIMESTAMP.off + F_TIMESTAMP.len == F_HEADER_CRC.off);
static_assert(F_HEADER_CRC.off + F_HEADER_CRC.len == HEADER_SIZE);

// The header CRC covers everything before itself.
inline constexpr std::size_t HEADER_CRC_COVERAGE = F_HEADER_CRC.off;
static_assert(HEADER_CRC_COVERAGE == 28);

bool knownCompression(std::uint8_t v) noexcept {
    return v == static_cast<std::uint8_t>(Compression::None);
}

std::unexpected<DecodeError> err(DecodeStatus s, std::size_t at = 0,
                                 std::size_t needed = 0) noexcept {
    return std::unexpected(DecodeError{s, at, needed});
}

/// Steps 1-5 of the validation order. Shared by peekHeader and decodeRecord so
/// the order itself is written once.
std::expected<RecordHeader, DecodeError> parseHeader(
    std::span<const std::uint8_t> in) noexcept {
    // 1. Enough bytes for a header at all? Checked before the magic, so a short
    //    tail reports Truncated rather than BadMagic.
    if (in.size() < HEADER_SIZE) {
        return err(DecodeStatus::Truncated, 0, HEADER_SIZE);
    }

    const std::uint8_t* p = in.data();

    // 2. Is this a record start? Checked before any CRC: if these bytes are not
    //    a record, calling them corrupt would be a lie.
    if (load_u32_le(p + F_MAGIC.off) != RECORD_MAGIC) {
        return err(DecodeStatus::BadMagic, F_MAGIC.off);
    }

    // 3. Header CRC. NOTHING below this line may be read until it passes.
    const std::uint32_t stored = load_u32_le(p + F_HEADER_CRC.off);
    const std::uint32_t actual = crc32c(in.first(HEADER_CRC_COVERAGE));
    if (stored != actual) {
        return err(DecodeStatus::HeaderCrcMismatch, F_HEADER_CRC.off);
    }

    // The header is now known-good, so its fields can be trusted.
    RecordHeader h{};
    h.version = p[F_VERSION.off];
    h.compression = static_cast<Compression>(p[F_COMPRESSION.off]);
    h.flags = p[F_FLAGS.off];
    h.key_len = load_u32_le(p + F_KEY_LEN.off);
    h.payload_len = load_u64_le(p + F_PAYLOAD_LEN.off);
    h.timestamp_ns = load_i64_le(p + F_TIMESTAMP.off);

    // 4. Policy: intact bytes this build may or may not be willing to interpret.
    if (h.version != RECORD_VERSION) {
        return err(DecodeStatus::BadVersion, F_VERSION.off);
    }
    if (p[F_RESERVED.off] != 0) {
        return err(DecodeStatus::BadReserved, F_RESERVED.off);
    }
    const std::uint8_t criticalBits = h.flags & FLAG_CRITICAL_MASK;
    if ((criticalBits & ~KNOWN_CRITICAL_FLAGS) != 0) {
        return err(DecodeStatus::BadFlags, F_FLAGS.off);
    }
    // Box and Manifest are competing kinds. Tombstone may co-occur with either,
    // because deleting a box is Box|Tombstone.
    if (h.has(Flag::Box) && h.has(Flag::Manifest)) {
        return err(DecodeStatus::BadFlags, F_FLAGS.off);
    }
    if (!knownCompression(p[F_COMPRESSION.off])) {
        return err(DecodeStatus::BadCompression, F_COMPRESSION.off);
    }
    if (h.has(Flag::Tombstone) && h.payload_len != 0) {
        return err(DecodeStatus::TombstoneHasPayload, F_PAYLOAD_LEN.off);
    }

    // 5. Caps and overflow, before anything is reserved or copied.
    if (h.key_len > MAX_KEY_LEN) {
        return err(DecodeStatus::LengthOverflow, F_KEY_LEN.off);
    }
    if (h.payload_len > MAX_PAYLOAD_LEN) {
        return err(DecodeStatus::LengthOverflow, F_PAYLOAD_LEN.off);
    }
    // Both lengths are now within caps that comfortably fit size_t, so the sum
    // cannot wrap. Asserted rather than assumed.
    static_assert(static_cast<std::uint64_t>(MAX_KEY_LEN) + MAX_PAYLOAD_LEN + RECORD_OVERHEAD <
                  static_cast<std::uint64_t>(SIZE_MAX));

    return h;
}

}  // namespace

std::string_view describe(DecodeStatus s) noexcept {
    switch (s) {
        case DecodeStatus::Ok: return "ok";
        case DecodeStatus::Truncated: return "truncated (torn tail)";
        case DecodeStatus::BadMagic: return "bad magic (not a record)";
        case DecodeStatus::HeaderCrcMismatch: return "header checksum mismatch (bit rot)";
        case DecodeStatus::BodyCrcMismatch: return "body checksum mismatch (bit rot)";
        case DecodeStatus::BadVersion: return "unsupported format version";
        case DecodeStatus::BadReserved: return "reserved byte not zero";
        case DecodeStatus::BadFlags: return "unknown or illegal flag combination";
        case DecodeStatus::BadCompression: return "unsupported compression";
        case DecodeStatus::LengthOverflow: return "length past its cap";
        case DecodeStatus::TombstoneHasPayload: return "tombstone with a non-empty payload";
    }
    return "unknown";
}

std::int64_t nowNanos() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// --- Envelope ---------------------------------------------------------------

Envelope Envelope::fromParts(RecordHeader header, std::string key, Bytes payload) {
    Envelope e;
    e._header = header;
    e._key = std::move(key);
    e._payload = std::move(payload);
    return e;
}

Envelope Envelope::value(std::string key, Bytes payload, std::int64_t timestamp_ns) {
    RecordHeader h{};
    h.flags = 0;
    h.key_len = static_cast<std::uint32_t>(key.size());
    h.payload_len = payload.size();
    h.timestamp_ns = timestamp_ns;
    return fromParts(h, std::move(key), std::move(payload));
}

Envelope Envelope::value(std::string key, Bytes payload) {
    return value(std::move(key), std::move(payload), nowNanos());
}

Envelope Envelope::tombstone(std::string key, std::int64_t timestamp_ns) {
    RecordHeader h{};
    h.flags = static_cast<std::uint8_t>(Flag::Tombstone);
    h.key_len = static_cast<std::uint32_t>(key.size());
    h.payload_len = 0;
    h.timestamp_ns = timestamp_ns;
    return fromParts(h, std::move(key), Bytes{});
}

Envelope Envelope::tombstone(std::string key) {
    return tombstone(std::move(key), nowNanos());
}

Envelope Envelope::box(std::string key, Bytes meta, std::int64_t timestamp_ns) {
    RecordHeader h{};
    h.flags = static_cast<std::uint8_t>(Flag::Box);
    h.key_len = static_cast<std::uint32_t>(key.size());
    h.payload_len = meta.size();
    h.timestamp_ns = timestamp_ns;
    return fromParts(h, std::move(key), std::move(meta));
}

Envelope Envelope::box(std::string key, Bytes meta) {
    return box(std::move(key), std::move(meta), nowNanos());
}

std::size_t Envelope::encodedSize() const noexcept {
    return RECORD_OVERHEAD + _key.size() + _payload.size();
}

void Envelope::encodeTo(Bytes& out) const {
    // Throwing here is deliberate: an oversized key on an Envelope the caller
    // just built is a bug in the caller, not untrusted input. decodeRecord,
    // which does handle untrusted input, never throws.
    if (_key.size() > MAX_KEY_LEN) {
        throw std::length_error("Envelope::encodeTo: key exceeds MAX_KEY_LEN");
    }
    if (_payload.size() > MAX_PAYLOAD_LEN) {
        throw std::length_error("Envelope::encodeTo: payload exceeds MAX_PAYLOAD_LEN");
    }
    if (isTombstone() && !_payload.empty()) {
        throw std::length_error("Envelope::encodeTo: tombstone with a non-empty payload");
    }

    const std::size_t base = out.size();
    out.insert(out.end(), HEADER_SIZE, 0);
    std::uint8_t* h = out.data() + base;

    store_u32_le(h + F_MAGIC.off, RECORD_MAGIC);
    h[F_VERSION.off] = RECORD_VERSION;
    h[F_COMPRESSION.off] = static_cast<std::uint8_t>(_header.compression);
    h[F_FLAGS.off] = _header.flags;
    h[F_RESERVED.off] = 0;
    store_u32_le(h + F_KEY_LEN.off, static_cast<std::uint32_t>(_key.size()));
    store_u64_le(h + F_PAYLOAD_LEN.off, _payload.size());
    store_i64_le(h + F_TIMESTAMP.off, _header.timestamp_ns);
    store_u32_le(h + F_HEADER_CRC.off,
                 crc32c(std::span<const std::uint8_t>{h, HEADER_CRC_COVERAGE}));

    const auto* keyBytes = reinterpret_cast<const std::uint8_t*>(_key.data());
    out.insert(out.end(), keyBytes, keyBytes + _key.size());
    out.insert(out.end(), _payload.begin(), _payload.end());

    // Chained rather than concatenated into a temporary: the key and the payload
    // are checksummed as if adjacent, without copying them to make them so.
    std::uint32_t body = crc32c_continue(0u, {keyBytes, _key.size()});
    body = crc32c_continue(body, _payload);
    put_u32_le(out, body);
}

Bytes Envelope::encode() const {
    Bytes out;
    out.reserve(encodedSize());
    encodeTo(out);
    return out;
}

// --- Decoding ---------------------------------------------------------------

std::expected<RecordHeader, DecodeError> peekHeader(
    std::span<const std::uint8_t> input) noexcept {
    auto h = parseHeader(input);
    if (!h) return std::unexpected(h.error());

    // 6. Is the whole record present? `needed` is trustworthy here because the
    //    header CRC passed in step 3.
    const std::size_t total = h->totalSize();
    if (input.size() < total) {
        return err(DecodeStatus::Truncated, HEADER_SIZE, total);
    }
    return h;
}

std::expected<Decoded, DecodeError> decodeRecord(
    std::span<const std::uint8_t> input) noexcept {
    auto header = peekHeader(input);
    if (!header) return std::unexpected(header.error());

    const std::size_t keyLen = header->key_len;
    const std::size_t payloadLen = static_cast<std::size_t>(header->payload_len);
    const std::size_t total = header->totalSize();

    const std::span<const std::uint8_t> keySpan = input.subspan(HEADER_SIZE, keyLen);
    const std::span<const std::uint8_t> payloadSpan =
        input.subspan(HEADER_SIZE + keyLen, payloadLen);

    // 7. Body CRC over (key ++ payload).
    const std::size_t trailerAt = HEADER_SIZE + keyLen + payloadLen;
    const std::uint32_t storedBody = load_u32_le(input.data() + trailerAt);
    std::uint32_t actualBody = crc32c_continue(0u, keySpan);
    actualBody = crc32c_continue(actualBody, payloadSpan);
    if (storedBody != actualBody) {
        // The key bytes are themselves only covered by this CRC, so they are
        // suspect too. A scrub may still report them -- "something near
        // photos/2026/beach.jpg is damaged" beats "offset 4019232 is damaged" --
        // but they must never be written back into the index.
        return err(DecodeStatus::BodyCrcMismatch, trailerAt);
    }

    std::string key(reinterpret_cast<const char*>(keySpan.data()), keySpan.size());
    Bytes payload(payloadSpan.begin(), payloadSpan.end());

    Decoded out;
    out.envelope = Envelope::fromParts(*header, std::move(key), std::move(payload));
    out.consumed = total;
    return out;
}

// --- Key paths --------------------------------------------------------------

namespace keys {

bool is_valid(std::string_view key) noexcept {
    if (key.empty() || key.size() > MAX_KEY_LEN) return false;
    if (key.front() == SEPARATOR || key.back() == SEPARATOR) return false;
    // No empty segment anywhere, i.e. no "//".
    for (std::size_t i = 1; i < key.size(); ++i) {
        if (key[i] == SEPARATOR && key[i - 1] == SEPARATOR) return false;
    }
    return true;
}

std::string_view parent(std::string_view key) noexcept {
    const std::size_t slash = key.rfind(SEPARATOR);
    if (slash == std::string_view::npos) return {};
    return key.substr(0, slash);
}

std::string_view leaf(std::string_view key) noexcept {
    const std::size_t slash = key.rfind(SEPARATOR);
    if (slash == std::string_view::npos) return key;
    return key.substr(slash + 1);
}

bool is_within(std::string_view key, std::string_view box) noexcept {
    if (box.empty()) return false;
    // A box is not within itself.
    if (key.size() <= box.size()) return false;
    if (key.substr(0, box.size()) != box) return false;
    // The boundary check is the point: without it "photos/2026x" would read as
    // inside "photos/2026".
    return key[box.size()] == SEPARATOR;
}

}  // namespace keys

}  // namespace swiftsearch
