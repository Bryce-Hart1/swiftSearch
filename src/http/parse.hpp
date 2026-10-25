#pragma once
/**
 * The HTTP/1.1 request-head parser.
 *
 * This file is a deliberate transliteration of record.hpp's contract, because
 * the two problems are the same problem: decode a structure out of a buffer
 * that may not hold all of it yet, where "not all of it yet" is the normal case
 * rather than an error.
 *
 *   decodeRecord                      parseRequestHead
 *   ------------------------------    ------------------------------
 *   total function over a span        total function over a span
 *   never throws                      never throws
 *   DecodeStatus::Truncated is        ParseStatus::Incomplete is
 *     the expected result of a          the expected result of a
 *     torn tail                         partial read
 *   validation ORDER is documented    validation ORDER is documented
 *     and load-bearing                  and load-bearing
 *   caps checked before any copy      caps checked before any copy
 *
 * There are no sockets in here and there must never be any. That is what lets
 * the whole parser be tested by feeding it one byte at a time.
 *
 * VALIDATION ORDER, which is the design:
 *
 *   1. No blank line yet, and under the head cap   -> Incomplete.
 *      Checked FIRST, so a head split across reads is incomplete rather than
 *      malformed -- the counterpart of record.hpp reporting Truncated rather
 *      than BadMagic for a short tail. Reversing 1 and 2 would make a client
 *      that sends a large head in small pieces fail intermittently depending on
 *      how the kernel happened to split it.
 *   2. No blank line, at or over the cap           -> HeadTooLarge (431).
 *   3. Request line: method, target, version.
 *   4. Header lines: shape, name token, no obs-fold.
 *   5. FRAMING. Content-Length and Transfer-Encoding together is refused
 *      outright, never reconciled: disagreeing about which one wins is the
 *      canonical request-smuggling vector, and the only safe answer is to
 *      decline the request.
 *   6. Caps on the declared body length, before anything is reserved. Same
 *      discipline record.hpp applies to key_len and payload_len: a hostile
 *      length field must not be able to drive an allocation.
 *   7. Connection semantics (keep-alive, Expect).
 *
 * LIFETIME: RequestHead borrows from the input span -- target and every header
 * name and value are views into it. That is what keeps the parser allocation-
 * light and testable, and it is why Request in message.hpp is a separate owning
 * type. Do not store a RequestHead past the buffer it was parsed from.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "record.hpp"
#include "status.hpp"

namespace swiftsearch::http {

/// The methods the dock implements. Anything else is a 405; this is not an
/// enumeration of HTTP, it is an enumeration of what has a route.
enum class Method : std::uint8_t {
    Unknown = 0,
    Get,
    Head,
    Put,
    Delete,
};

std::string_view methodName(Method m) noexcept;

enum class ParseStatus : std::uint8_t {
    Ok = 0,
    Incomplete,           // no blank line yet -> read more. NOT a failure.
    HeadTooLarge,         // past maxHeadBytes with no blank line      -> 431
    TooManyHeaders,       // past maxHeaders                           -> 431
    BadRequestLine,       // not "METHOD SP target SP HTTP/x.y"        -> 400
    BadMethod,            // well-formed but not one this dock serves  -> 405
    BadVersion,           // not HTTP/1.0 or HTTP/1.1                  -> 505
    BadTarget,            // empty, or not origin-form ("/...")        -> 400
    BadHeader,            // no colon, empty name, bad name byte       -> 400
    ObsFold,              // a continuation line                       -> 400
    MissingHost,          // HTTP/1.1 without Host                     -> 400
    BadContentLength,     // non-digit, overflow, or disagreeing dups  -> 400
    ConflictingFraming,   // Content-Length AND Transfer-Encoding      -> 400
    UnsupportedTransfer,  // any Transfer-Encoding at all              -> 501
    BodyTooLarge,         // Content-Length past the cap               -> 413
};

struct ParseError {
    ParseStatus status = ParseStatus::Ok;
    std::size_t at = 0;  // byte offset in the input where it was detected
};

/// Read more bytes and try again. The normal path on every partial read.
constexpr bool isIncomplete(ParseStatus s) noexcept { return s == ParseStatus::Incomplete; }

/// The whole ParseStatus -> HTTP status mapping, in one place, so no route can
/// invent its own answer for a parse failure.
constexpr Status statusFor(ParseStatus s) noexcept {
    switch (s) {
        case ParseStatus::Ok:
        case ParseStatus::Incomplete:       return Status::Ok;
        case ParseStatus::HeadTooLarge:
        case ParseStatus::TooManyHeaders:   return Status::RequestHeaderFieldsTooLarge;
        case ParseStatus::BadMethod:        return Status::MethodNotAllowed;
        case ParseStatus::BadVersion:       return Status::HttpVersionNotSupported;
        case ParseStatus::UnsupportedTransfer: return Status::NotImplemented;
        case ParseStatus::BodyTooLarge:     return Status::ContentTooLarge;
        case ParseStatus::BadRequestLine:
        case ParseStatus::BadTarget:
        case ParseStatus::BadHeader:
        case ParseStatus::ObsFold:
        case ParseStatus::MissingHost:
        case ParseStatus::BadContentLength:
        case ParseStatus::ConflictingFraming: return Status::BadRequest;
    }
    return Status::BadRequest;
}

std::string_view describe(ParseStatus s) noexcept;

/// A header, as views into the parsed buffer.
struct HeaderRef {
    std::string_view name;
    std::string_view value;
};

struct HeadLimits {
    /// 8 KB is generous for a head whose only variable part is a key, and small
    /// enough that a client dribbling bytes cannot grow a buffer. This is the
    /// slow-loris bound.
    std::size_t maxHeadBytes = 8u * 1024u;
    std::size_t maxHeaders = 64;
    std::uint64_t maxBodyBytes = MAX_PAYLOAD_LEN;
};

struct RequestHead {
    Method method = Method::Unknown;
    std::string_view target;  // origin-form, still percent-encoded
    int minorVersion = 1;     // the 1 in HTTP/1.1

    bool hasContentLength = false;
    std::uint64_t contentLength = 0;
    /// Distinguishes "Content-Length: 0" from no Content-Length at all, which
    /// is the difference between an empty body and a PUT that owes a 411.
    bool hasBody() const noexcept { return hasContentLength && contentLength > 0; }

    bool keepAlive = true;
    bool expectContinue = false;

    /// Bytes consumed, including the terminating blank line. The body starts
    /// here and the caller advances by it.
    std::size_t headLength = 0;

    std::vector<HeaderRef> headers;

    /// Case-insensitive, first match. HTTP field names are case-insensitive, so
    /// a case-sensitive lookup is a bug that works until a client changes its
    /// capitalisation.
    std::optional<std::string_view> header(std::string_view name) const noexcept;
};

/// The offset just past the blank line that ends the head, or nullopt if it has
/// not arrived.
///
/// Accepts both CRLF and a bare LF as a line terminator. RFC 9112 permits
/// recognising a lone LF, and a hand-written client or a shell heredoc will send
/// one; responses are always written with CRLF regardless. Being lenient on
/// input and strict on output is the only combination that does not punish
/// someone for testing with `printf`.
std::optional<std::size_t> findHeadEnd(std::span<const std::uint8_t> in) noexcept;

std::expected<RequestHead, ParseError> parseRequestHead(
    std::span<const std::uint8_t> in, const HeadLimits& limits);

/// Case-insensitive ASCII comparison, for field names and token values.
bool iEquals(std::string_view a, std::string_view b) noexcept;

/// True when `list` -- a comma-separated field value such as Connection -- has
/// `token` as one of its elements, ignoring case and surrounding whitespace.
bool hasListToken(std::string_view list, std::string_view token) noexcept;

}  // namespace swiftsearch::http
