#pragma once
/**
 * Owning request and response types, and response serialization.
 *
 * RequestHead (parse.hpp) borrows from the read buffer, which is correct for a
 * parser and wrong for anything that outlives the parse. A Request is handed to
 * a worker thread while the reactor goes on reading, and the buffer it came
 * from may be reused or freed in between, so Request owns its bytes.
 *
 * The copy is cheap where it matters: the target and headers are a few hundred
 * bytes and are copied, while the body -- up to 64 MB -- is MOVED. Nothing here
 * ever copies a payload.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bytes.hpp"
#include "parse.hpp"
#include "status.hpp"

namespace swiftsearch::http {

/// Spelled here rather than taken from mime.hpp, so message.* does not depend
/// on the content-type tables just to label its own JSON bodies.
inline constexpr std::string_view JSON_TYPE_LITERAL = "application/json";

class Request {
public:
    Method method = Method::Unknown;
    std::string target;  // origin-form, still percent-encoded
    int minorVersion = 1;
    bool keepAlive = true;
    bool expectContinue = false;
    std::vector<std::pair<std::string, std::string>> headers;
    Bytes body;

    /// Copies the head's small fields and MOVES the body in.
    static Request own(const RequestHead& head, Bytes body);

    /// Case-insensitive, first match.
    std::optional<std::string_view> header(std::string_view name) const noexcept;
};

class Response {
public:
    Status status = Status::Ok;
    std::vector<std::pair<std::string, std::string>> headers;
    Bytes body;

    /// Forces Connection: close. Set for every error that leaves the stream in
    /// an unknown state -- a rejected body means unread bytes are still queued,
    /// so the next thing on the socket is not a request.
    bool closeAfter = false;

    /// Content-Length is still sent, but the body is not. This is what makes
    /// HEAD identical to GET from a client's point of view.
    bool omitBody = false;

    Response& set(std::string name, std::string value);

    static Response empty(Status s);
    /// A JSON body, already serialized.
    static Response json(Status s, std::string body);
    /// The standard error shape: {"error":…,"code":…,"status":…}.
    static Response error(Status s, std::string_view message);
    /// An error that also closes the connection.
    static Response fatal(Status s, std::string_view message);

    bool hasHeader(std::string_view name) const noexcept;
};

/// Writes the status line and headers, CRLF-terminated, ending with the blank
/// line. Always CRLF regardless of what the request used.
///
/// Three headers are added here rather than at each call site, because a route
/// that forgets one is a bug nobody notices:
///   - Content-Length, always, even for an empty or omitted body. A response
///     without it has no frame, and the client has to wait for a close to know
///     it ended.
///   - Connection, matching keepAlive, so the client never has to guess.
///   - X-Content-Type-Options: nosniff. A deliberately conservative
///     Content-Type (see mime.hpp) is worth nothing if the browser is free to
///     sniff a different one out of the bytes.
void serializeHead(const Response& r, bool keepAlive, std::string& out);

}  // namespace swiftsearch::http
