#pragma once
/**
 * Request-target handling: splitting, percent-decoding, and turning a URL path
 * into a store key.
 *
 * This file is the security boundary of the whole server. A key is a path
 * ('/'-separated, per swiftsearch::keys), and the URL path IS the key, so
 * everything a client can put in a URL arrives here first. Every function is
 * total: no throws, no out-of-range reads, and a hostile input produces a status
 * rather than a surprise.
 *
 * WHY '%2F' IS REFUSED RATHER THAN DECODED. If an encoded slash were decoded
 * before the segment checks, a client could smuggle a separator past them and
 * invent a path shape the checks never saw; if it were decoded after, the key
 * stored would not be the key validated. Both orderings are a question with a
 * wrong answer, so the encoded form is simply not accepted. Nothing legitimate
 * needs it: a key containing a literal slash is a key with another segment.
 *
 * WHY '.' AND '..' ARE REFUSED even though keys::is_valid permits them.
 * keys::is_valid is a path-SHAPE rule (non-empty, no leading or trailing
 * separator, no empty segment); it is deliberately not a security check, and it
 * lives in the format layer where policy does not belong. Dot segments have a
 * meaning in URLs that they do not have in keys, so a client writing one means
 * something the store cannot honour. Refusing is the only answer that cannot
 * surprise anyone later -- in particular if a key is ever mapped onto a
 * filesystem path.
 *
 * WHY A KEY MUST BE WELL-FORMED UTF-8, even though the store treats it as
 * uninterpreted bytes. Percent-decoding can produce any byte sequence at all,
 * including ones that are not valid UTF-8. JSON has no way to represent such a
 * byte: \uXXXX encodes code points, not bytes, so a key containing 0xFF cannot
 * appear in a listing response without making the whole document unparseable.
 * The choice is therefore to reject it here, at the edge, or to emit broken
 * JSON later. Validating once on the way in is the only option that keeps every
 * key representable everywhere it has to appear, and it is why json_write can
 * pass bytes >= 0x80 through untouched.
 */

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace swiftsearch::http {

enum class UrlStatus : std::uint8_t {
    Ok = 0,
    NoPrefixMatch,     // the path is not under the route prefix at all
    Empty,             // nothing left after the prefix
    BadPercent,        // '%' not followed by two hex digits
    EncodedSeparator,  // %2F / %2f -- see the note above
    ControlByte,       // a byte < 0x20, or 0x7F, after decoding
    DotSegment,        // a '.' or '..' segment
    TooLong,           // past MAX_KEY_LEN
    NotAPath,          // fails keys::is_valid
    BadUtf8,           // not well-formed UTF-8 -- see the note below
};

std::string_view describe(UrlStatus s) noexcept;

/// The path and query halves of a request target, split at the first '?'.
/// The query excludes the '?' and is empty when there is none. A '#' fragment
/// is never sent by a client, so it is not special-cased; it would land in the
/// query and fail validation downstream.
struct Target {
    std::string_view path;
    std::string_view query;
};

Target splitTarget(std::string_view target) noexcept;

/// Decodes %XX escapes. '+' is left alone: it means a space only in a
/// form-encoded query, never in a path, and treating it as one here would
/// corrupt every key with a plus in it.
std::expected<std::string, UrlStatus> percentDecode(std::string_view in);

/// Strips `prefix` from `path` and returns the store key, fully validated
/// against every rule in this file's header comment. `prefix` is matched
/// literally and must end with '/' (for example "/v1/blobs/").
std::expected<std::string, UrlStatus> keyFromPath(std::string_view path,
                                                  std::string_view prefix);

/// The raw, still-encoded value of `name` in a query string, or nullopt.
/// Raw because a value may need decoding (a cursor is a key) and a view cannot
/// own the decoded bytes; the caller runs percentDecode when it matters.
/// A valueless parameter ("?deep") returns an empty view, which is
/// distinguishable from nullopt.
std::optional<std::string_view> queryParam(std::string_view query,
                                           std::string_view name) noexcept;

/// True when `s` is well-formed UTF-8. Rejects overlong encodings, surrogate
/// halves and anything above U+10FFFF, all of which some decoders accept and
/// then disagree about. Exposed because header values need the same check.
bool isValidUtf8(std::string_view s) noexcept;

/// Parses a non-negative decimal integer in full. Rejects a partial parse, a
/// sign, whitespace, emptiness, and anything that would overflow -- which is
/// the whole point, since these come from a URL. Used for ?limit=.
std::optional<std::uint64_t> parseDecimal(std::string_view s) noexcept;

}  // namespace swiftsearch::http
