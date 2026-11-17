#include "compress.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#include "crc32c.hpp"

namespace swiftsearch {
namespace {

constexpr std::size_t MIN_MATCH = 4;
constexpr std::size_t MAX_OFFSET = 65535;  // what two offset bytes can say
constexpr unsigned HASH_BITS = 16;
constexpr std::size_t CHAIN_SIZE = std::size_t{1} << 16;  // > MAX_OFFSET, so a live link is never overwritten
constexpr std::size_t CHAIN_MASK = CHAIN_SIZE - 1;
constexpr std::uint8_t NIBBLE_MORE = 15;

static_assert(CHAIN_SIZE > MAX_OFFSET);
// Positions are kept as int32 in the match tables; every payload fits.
static_assert(MAX_PAYLOAD_LEN < static_cast<std::uint64_t>(INT32_MAX));

std::uint32_t hash4(const std::uint8_t* p) noexcept {
    return (load_u32_le(p) * 2654435761u) >> (32 - HASH_BITS);
}

/// The rest of a count whose nibble was 15: 255s, then the remainder.
void putMore(Bytes& out, std::size_t rest) {
    while (rest >= 255) {
        out.push_back(255);
        rest -= 255;
    }
    out.push_back(static_cast<std::uint8_t>(rest));
}

/// One sequence. match_len 0 is the closing literals-only one.
void putSequence(Bytes& out, std::span<const std::uint8_t> literals, std::size_t match_len, std::size_t offset) {
    const std::size_t lit = literals.size();
    const std::size_t extra = match_len == 0 ? 0 : match_len - MIN_MATCH;
    const auto nib = [](std::size_t n) { return static_cast<std::uint8_t>(std::min<std::size_t>(n, NIBBLE_MORE)); };
    out.push_back(static_cast<std::uint8_t>(nib(lit) << 4 | (match_len == 0 ? 0 : nib(extra))));
    if (lit >= NIBBLE_MORE) putMore(out, lit - NIBBLE_MORE);
    out.insert(out.end(), literals.begin(), literals.end());
    if (match_len == 0) return;
    put_u16_le(out, static_cast<std::uint16_t>(offset));
    if (extra >= NIBBLE_MORE) putMore(out, extra - NIBBLE_MORE);
}

}  // namespace


Bytes lzCompress(std::span<const std::uint8_t> in, int level) {
    level = std::clamp(level, MIN_COMPRESS_LEVEL, MAX_COMPRESS_LEVEL);
    const unsigned max_probes = 1u << (level - 1);
    // a match this long is good enough to stop looking for a longer one
    const std::size_t nice_len = std::size_t{16} << level;

    const std::size_t n = in.size();
    Bytes out;
    out.reserve(LZ_FRAME_HEADER + n + n / 255 + 16);
    put_u64_le(out, n);
    put_u32_le(out, crc32c(in));

    std::vector<std::int32_t> head(std::size_t{1} << HASH_BITS, -1);
    std::vector<std::int32_t> prev(CHAIN_SIZE, -1);
    const std::uint8_t* p = in.data();
    const auto insert = [&](std::size_t at) {
        std::int32_t& slot = head[hash4(p + at)];
        prev[at & CHAIN_MASK] = slot;
        slot = static_cast<std::int32_t>(at);
    };

    std::size_t anchor = 0;  // first byte not yet written
    std::size_t i = 0;
    while (i + MIN_MATCH <= n) {
        std::size_t best_len = 0;
        std::size_t best_off = 0;
        const std::size_t room = n - i;
        std::int32_t cand = head[hash4(p + i)];
        for (unsigned probes = max_probes; cand >= 0 && probes > 0; --probes) {
            const std::size_t c = static_cast<std::size_t>(cand);
            const std::size_t off = i - c;
            if (off > MAX_OFFSET) break;  // the chain only gets older from here
            // best_len < room, or the loop would have stopped; cheap reject first
            if (p[c + best_len] == p[i + best_len]) {
                std::size_t len = 0;
                while (len < room && p[c + len] == p[i + len]) ++len;
                if (len > best_len) {
                    best_len = len;
                    best_off = off;
                    if (len >= nice_len || len == room) break;
                }
            }
            const std::int32_t next = prev[c & CHAIN_MASK];
            if (next >= cand) break;  // a slot reused by a newer position: the chain ended
            cand = next;
        }

        if (best_len >= MIN_MATCH) {
            putSequence(out, in.subspan(anchor, i - anchor), best_len, best_off);
            const std::size_t stop = i + best_len;
            for (; i < stop; ++i) {
                if (i + MIN_MATCH <= n) insert(i);
            }
            anchor = i;
        } else {
            insert(i);
            ++i;
        }
    }
    putSequence(out, in.subspan(anchor), 0, 0);
    return out;
}


std::optional<std::uint64_t> lzFrameSize(std::span<const std::uint8_t> frame) noexcept {
    if (frame.size() < LZ_FRAME_HEADER) return std::nullopt;
    const std::uint64_t raw_len = load_u64_le(frame.data());
    if (raw_len > MAX_PAYLOAD_LEN) return std::nullopt;
    return raw_len;
}


std::expected<Bytes, std::string> lzDecompress(std::span<const std::uint8_t> frame) noexcept {
    using R = std::expected<Bytes, std::string>;
    const auto bad = [](const char* why) -> R { return std::unexpected(std::string(why)); };
    try {
        if (frame.size() < LZ_FRAME_HEADER + 1) return bad("lz frame is truncated");
        const auto size = lzFrameSize(frame);
        if (!size) return bad("lz frame says the value is past the size cap");
        const std::size_t raw_len = static_cast<std::size_t>(*size);
        // No input byte makes more than 255 output bytes, so a length past that is a lie,
        // refused before it can size a buffer
        if (raw_len / 255 > frame.size() - LZ_FRAME_HEADER) return bad("lz frame is too short for the value it claims");
        const std::uint32_t want_crc = load_u32_le(frame.data() + 8);

        Bytes out(raw_len);
        std::size_t op = 0;
        std::size_t ip = LZ_FRAME_HEADER;
        const std::size_t end = frame.size();
        const std::uint8_t* in = frame.data();

        // Adds the bytes after a nibble of 15. Stops as soon as the count is past what is
        // left to produce, so a run of 255s cannot overflow it.
        const auto more = [&](std::size_t& count, std::size_t limit) {
            for (;;) {
                if (ip >= end) return false;
                const std::uint8_t b = in[ip++];
                count += b;
                if (count > limit) return false;
                if (b != 255) return true;
            }
        };

        for (;;) {
            if (ip >= end) return bad("lz frame is truncated");
            const std::uint8_t token = in[ip++];

            std::size_t lit = token >> 4;
            if (lit == NIBBLE_MORE && !more(lit, raw_len - op)) return bad("lz literal run is past the end");
            if (lit > raw_len - op || lit > end - ip) return bad("lz literal run is past the end");
            if (lit != 0) std::memcpy(out.data() + op, in + ip, lit);
            ip += lit;
            op += lit;
            if (ip == end) break;  // the closing literals-only sequence

            if (end - ip < 2) return bad("lz frame is truncated");
            const std::size_t offset = load_u16_le(in + ip);
            ip += 2;
            if (offset == 0 || offset > op) return bad("lz match reaches before the start");

            std::size_t match = token & 0x0F;
            if (match == NIBBLE_MORE && !more(match, raw_len - op)) return bad("lz match is past the end");
            match += MIN_MATCH;
            if (match > raw_len - op) return bad("lz match is past the end");

            std::uint8_t* dst = out.data() + op;
            const std::uint8_t* src = dst - offset;
            if (offset >= match) {
                std::memcpy(dst, src, match);
            } else {
                for (std::size_t k = 0; k < match; ++k) dst[k] = src[k];  // overlaps what it writes
            }
            op += match;
        }
        if (op != raw_len) return bad("lz frame ends before the value does");
        if (crc32c(out) != want_crc) return bad("lz value checksum mismatch");
        return out;
    } catch (const std::bad_alloc&) {
        return bad("out of memory decompressing a value");
    }
}


std::optional<Bytes> compressPayload(std::span<const std::uint8_t> value, int level) {
    if (value.size() < MIN_COMPRESS_BYTES) return std::nullopt;
    Bytes frame = lzCompress(value, level);
    if (frame.size() * 100 > value.size() * (100 - MIN_SAVING_PERCENT)) return std::nullopt;
    return frame;
}


std::expected<Bytes, std::string> decompressPayload(Compression how, std::span<const std::uint8_t> stored) noexcept {
    switch (how) {
        case Compression::None:
            try {
                return Bytes(stored.begin(), stored.end());
            } catch (const std::bad_alloc&) {
                return std::unexpected(std::string("out of memory reading a value"));
            }
        case Compression::Lz:
            return lzDecompress(stored);
        case Compression::Zstd:
            break;
    }
    return std::unexpected(std::string(describe(DecodeStatus::BadCompression)));
}


std::optional<std::uint64_t> storedValueSize(const RecordHeader& header,
                                             std::span<const std::uint8_t> stored) noexcept {
    if (header.compression == Compression::Lz) return lzFrameSize(stored);
    if (header.compression == Compression::None) return header.payload_len;
    return std::nullopt;
}


bool worthCompressing(const RecordHeader& h) noexcept {
    if (h.compression != Compression::None) return false;
    if (h.has(Flag::Tombstone) || h.has(Flag::Box) || h.has(Flag::Manifest) || h.has(Flag::CompressTried)) {
        return false;
    }
    if (h.payload_len < MIN_COMPRESS_BYTES) return false;
    switch (h.var_type) {
        case VAR_TYPE::STR:
        case VAR_TYPE::BIN:
        case VAR_TYPE::UNSET:
            return true;
        default:
            return false;  // IMG and VIDEO are compressed already; the numbers are a few bytes
    }
}


Bytes recompressRecord(const Envelope& record, int level, CompressReport& report) {
    RecordHeader h = record.header();
    const auto payload = record.payload();
    report.payloadBytesIn += payload.size();
    if (auto frame = compressPayload(payload, level)) {
        h.compression = Compression::Lz;
        h.payload_len = frame->size();
        report.recordsCompressed += 1;
        report.payloadBytesOut += frame->size();
        return Envelope::fromParts(h, record.key(), std::move(*frame)).encode();
    }
    h.flags |= static_cast<std::uint8_t>(Flag::CompressTried);
    report.recordsLeftRaw += 1;
    report.payloadBytesOut += payload.size();
    return Envelope::fromParts(h, record.key(), Bytes(payload.begin(), payload.end())).encode();
}

}  // namespace swiftsearch
