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
 * VIDEO is a real VAR_TYPE value (appended as 9), so a video is served with a
 * container type a browser will play rather than application/octet-stream.
 *
 * A playable Content-Type is necessary but not sufficient for video in a
 * browser: seeking needs HTTP range requests, and Safari in particular may
 * decline to start playback at all without Accept-Ranges. Those are now built
 * (src/http/conditional.*), so a video both plays and scrubs. What is still
 * ahead is making a range read CHEAP -- today the slice is cut from a payload
 * the store already returned whole, so seeking into a 60 MB video still reads
 * 60 MB. That is the chunk-and-manifest work in section 10 of the design notes.
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
