#pragma once
/**
 * Content-Type, in both directions.
 *
 * A record's var_type says what CLASS of value the payload is; the key's
 * extension says which member of that class. Neither alone is enough: VAR_TYPE
 * has one IMG value and cannot distinguish JPEG from PNG, while an extension is
 * client-supplied text that should not by itself decide how a browser treats
 * bytes. So the type is the class from the record, refined by the extension,
 * and the extension is only ever consulted within the class the record names.
 *
 * TWO TYPES ARE DELIBERATELY NOT SERVED, and both are the same bug:
 *
 *   - text/html for a stored value. Serving client-uploaded HTML from the same
 *     origin as the API is stored XSS against any future web UI on that origin.
 *   - image/svg+xml. An SVG is a scriptable XML document, and a GET by URL is a
 *     top-level navigation, which is exactly the context where that script
 *     runs. In an <img> tag it would be inert, but the server cannot tell the
 *     two apart.
 *
 * Both fall back to a type a browser will not execute. This costs inline
 * display of two formats and is revisitable once blobs are served from an
 * origin of their own, which is the actual fix. Every response also carries
 * X-Content-Type-Options: nosniff, because a conservative Content-Type is worth
 * nothing if the browser is free to guess a different one.
 *
 * TODO, and it is a real gap rather than an oversight: VAR_TYPE has no VIDEO
 * value, so a video stores as BIN and is served as application/octet-stream,
 * which no browser will play inline. Appending VIDEO to VAR_TYPE is the fix and
 * is cheap -- the byte has 247 values spare -- but it is a format decision, not
 * one for this file to make quietly.
 */

#include <string>
#include <string_view>

#include "var_type.hpp"

namespace swiftsearch::http {

inline constexpr std::string_view OCTET_STREAM = "application/octet-stream";
inline constexpr std::string_view PLAIN_TEXT = "text/plain; charset=utf-8";
inline constexpr std::string_view JSON_TYPE = "application/json";

/// The lowercase extension of the key's last segment, without the dot. Empty
/// when there is none, when the name starts with the dot (a dotfile has no
/// extension), or when the extension is implausibly long.
std::string extensionOf(std::string_view key);

/// What to put in Content-Type when serving this value.
std::string_view contentTypeFor(VAR_TYPE t, std::string_view key);

/// The var_type to record for a PUT whose client sent no X-SwiftSearch-Type.
/// Total: falls back to BIN, which is "raw bytes of unstated kind" and is
/// always a true statement about a payload.
VAR_TYPE inferVarType(std::string_view contentType, std::string_view key);

}  // namespace swiftsearch::http
