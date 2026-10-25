#pragma once
/**
 * Conditional and partial GETs: ETag matching and Range parsing.
 *
 * RFC 9110 keeps these in separate sections (13 for conditional requests, 14
 * for range requests) but at this layer they are one decision -- what does this
 * GET actually return -- and they have a required ORDER that only makes sense
 * seen together. Hence one file.
 *
 * PRECEDENCE, which is not arbitrary:
 *
 *   1. If-None-Match matches  -> 304, and Range is never looked at. A client
 *      that already holds the bytes does not need a slice of them.
 *   2. If-Range present and not a STRONG match -> the Range is ignored and the
 *      whole representation is sent. The point of If-Range is "give me the rest
 *      of what I was downloading, but only if it has not changed underneath
 *      me"; answering with a slice of different content would corrupt the
 *      client's file silently.
 *   3. Range satisfiable -> 206. Unsatisfiable -> 416. Malformed -> ignored,
 *      send 200.
 *
 * TWO COMPARISON STRENGTHS, and mixing them up is a real bug:
 *   - If-None-Match uses WEAK comparison, so `W/"x"` matches `"x"`.
 *   - If-Range uses STRONG comparison, so a weak tag never matches anything.
 * That asymmetry is deliberate in the spec: a weak validator may be equal for
 * representations that differ byte-for-byte, which is fine for "do I need to
 * re-fetch?" and catastrophic for "can I splice this onto what I already have?"
 *
 * MALFORMED RANGES ARE IGNORED, NOT REJECTED. RFC 9110 says a server must
 * ignore a Range it cannot understand, so a typo yields the whole file rather
 * than a 416. 416 is reserved for a range that parses fine and asks for bytes
 * that do not exist, which is a different mistake and worth telling apart.
 *
 * Every function here is total and allocation-light, in the house style: no
 * throws, no out-of-range reads, a hostile header yields a status.
 */

#include <cstdint>
#include <string>
#include <string_view>

#include "sha256.hpp"

namespace swiftsearch::http {

/// A strong entity-tag for a content hash: the hex digest in double quotes.
/// Strong is correct because the tag IS the content -- two representations with
/// the same tag are byte-for-byte identical, which is exactly what a strong
/// validator promises.
std::string etagOf(const Digest& d);

/// Weak comparison, for If-None-Match. `*` matches any existing representation.
/// `header` may be a comma-separated list, and `W/` prefixes are ignored.
bool ifNoneMatchMatches(std::string_view header, std::string_view etag) noexcept;

/// Strong comparison, for If-Range. A single tag only; a weak tag never
/// matches, and neither does an HTTP-date (which this does not implement, so a
/// date simply fails and the Range is ignored -- the safe direction).
bool ifRangeMatches(std::string_view header, std::string_view etag) noexcept;

enum class RangeStatus : std::uint8_t {
    /// No Range header at all. Send the whole thing.
    Absent = 0,
    /// A single satisfiable range. Send 206.
    Ok,
    /// Parsed fine, asks for bytes that do not exist. Send 416.
    Unsatisfiable,
    /// Malformed, an unknown unit, or more than one range. MUST be ignored:
    /// send 200 with the whole representation.
    Ignored,
};

std::string_view describe(RangeStatus s) noexcept;

/// Inclusive on both ends, the way the wire format expresses it. `last` is the
/// index of the final byte, not one past it, so a one-byte range has
/// first == last.
struct ByteRange {
    std::uint64_t first = 0;
    std::uint64_t last = 0;

    std::uint64_t length() const noexcept { return last - first + 1; }
};

struct RangeResult {
    RangeStatus status = RangeStatus::Absent;
    ByteRange range{};
};

/// Parses a Range header against a known representation size.
///
/// Only `bytes` is understood, and only one range. Multiple ranges are Ignored
/// rather than implemented: a multipart/byteranges response is a second body
/// framing to get right, and nothing that matters here asks for one -- a video
/// element sends a single range per request.
RangeResult parseRange(std::string_view header, std::uint64_t size) noexcept;

/// "bytes 0-1023/2048", for a 206.
std::string contentRange(const ByteRange& r, std::uint64_t size);

/// "bytes */2048", for a 416. The size is the whole point of the header here:
/// it tells the client what it should have asked for.
std::string contentRangeUnsatisfiable(std::uint64_t size);

}  // namespace swiftsearch::http
