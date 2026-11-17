#pragma once
/**
 * Lossless compression for stored values: swiftSearch's own LZ codec, and the
 * pieces the store needs to rewrite records with it while it is idle.
 *
 * Nothing compresses on the write path. Put stores a value as it arrived, and the
 * background compressor (SwiftSearch::compress, run by Supervise once the store has
 * been idle for a while) rewrites the segments holding plain records, the same way
 * compaction does, with each eligible payload replaced by an LZ frame.
 *
 * FRAME (the payload of a record whose compression byte is Compression::Lz):
 *
 *   off  size  field
 *     0     8  raw_len   length of the value once decompressed, little-endian
 *     8     4  raw_crc   CRC32C of the decompressed value
 *    12   ...  sequences
 *
 * raw_len lets a reader check the size cap and size its buffer before it decodes a
 * byte, and lets replay put a value's real size in the table without decompressing.
 * raw_crc is an end-to-end check on the codec itself: the record's body CRC only
 * covers the compressed bytes, so a decoder bug would otherwise go unnoticed.
 *
 * SEQUENCE: one token byte, then literals, then a back-reference.
 *
 *   token      high nibble = literal count, low nibble = match length - 4.
 *              A nibble of 15 means the count goes on in the bytes that follow:
 *              each is added, and a 255 means another byte comes after it.
 *   literals   copied to the output as they are
 *   offset     2 bytes little-endian, 1 to 65535: how far back the match starts.
 *              A match may overlap what it is producing (offset 1 repeats a byte).
 *   (more match length bytes if the low nibble was 15)
 *
 * The last sequence is literals only: the input ends right after its literals, and
 * that is how the decoder knows it is done. Every frame ends with one, even an empty
 * input's (a lone 0x00 token).
 *
 * The decoder trusts nothing: every count, copy and offset is checked against the
 * input and against raw_len, the input must be used up exactly, and the CRC must
 * match. It reports an error and never reads or writes out of bounds.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "bytes.hpp"
#include "compact.hpp"
#include "record.hpp"

namespace swiftsearch {

/// Level is effort: how many earlier positions the encoder tries per byte, 2^(level-1).
inline constexpr int MIN_COMPRESS_LEVEL = 1;
inline constexpr int MAX_COMPRESS_LEVEL = 9;
inline constexpr int DEFAULT_COMPRESS_LEVEL = 3;

inline constexpr std::size_t LZ_FRAME_HEADER = 12;
/// Payloads smaller than this are not worth a frame header.
inline constexpr std::uint64_t MIN_COMPRESS_BYTES = 64;
/// A frame is only kept when it is at least this much smaller than the value.
inline constexpr std::uint64_t MIN_SAVING_PERCENT = 10;

/// What one SwiftSearch::compress() run did.
struct CompressReport {
    CompactReport rewrite;               ///< the segment rewrite underneath, as compaction reports it
    std::uint64_t recordsCompressed = 0; ///< records now stored as LZ frames
    std::uint64_t recordsLeftRaw = 0;    ///< tried, did not shrink enough, marked CompressTried
    std::uint64_t payloadBytesIn = 0;    ///< value bytes of every record tried
    std::uint64_t payloadBytesOut = 0;   ///< what those records' payloads take on disk now
};

/// Always produces a frame, even when it is bigger than the input. Level is clamped.
Bytes lzCompress(std::span<const std::uint8_t> in, int level = DEFAULT_COMPRESS_LEVEL);

/// Undoes lzCompress. Never throws; a bad frame is an error string.
std::expected<Bytes, std::string> lzDecompress(std::span<const std::uint8_t> frame) noexcept;

/// raw_len from a frame's header, or nullopt when the header is short or the length is past MAX_PAYLOAD_LEN.
std::optional<std::uint64_t> lzFrameSize(std::span<const std::uint8_t> frame) noexcept;

/// A frame when it saves at least MIN_SAVING_PERCENT, nullopt when the value is too
/// small to bother with or does not shrink enough.
std::optional<Bytes> compressPayload(std::span<const std::uint8_t> value, int level = DEFAULT_COMPRESS_LEVEL);

/// The value a payload stored with `how` holds. None hands the bytes back as they are.
std::expected<Bytes, std::string> decompressPayload(Compression how, std::span<const std::uint8_t> stored) noexcept;

/// The size a record's value has once decompressed, read from the frame header without
/// decompressing. nullopt for an Lz payload whose header will not read.
std::optional<std::uint64_t> storedValueSize(const RecordHeader& header,
                                             std::span<const std::uint8_t> stored) noexcept;

/**
 * True for a record background compression should try: a plain value (not a tombstone,
 * box or manifest), stored uncompressed, not already tried, of a type that is not
 * compressed already (STR, BIN or UNSET; IMG and VIDEO are, and the numbers are a few
 * bytes), and at least MIN_COMPRESS_BYTES long.
 */
bool worthCompressing(const RecordHeader& header) noexcept;

/**
 * The record re-encoded for storage: with its payload as an LZ frame when that pays,
 * otherwise unchanged but for the CompressTried flag, so it is not tried again. Key,
 * type, flags and timestamp are kept. Counts what it did in `report`.
 * Only for records worthCompressing accepts.
 */
Bytes recompressRecord(const Envelope& record, int level, CompressReport& report);

}  // namespace swiftsearch
