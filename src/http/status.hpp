#pragma once
/**
 * HTTP status codes this server can emit, and their reason phrases.
 *
 * Only the codes actually reachable from a route are listed. An enum of every
 * code in the registry would be a list of things no caller can produce, and the
 * compiler could then no longer tell anyone that a switch is missing a case.
 *
 * The numeric values are the wire values, so the enum is written with them
 * spelled out rather than relying on declaration order.
 */

#include <string_view>

namespace swiftsearch::http {

enum class Status : int {
    Ok                          = 200,
    Created                     = 201,
    NoContent                   = 204,
    PartialContent              = 206,  // reserved for Range; not emitted yet
    NotModified                 = 304,  // reserved for ETag; not emitted yet
    BadRequest                  = 400,
    NotFound                    = 404,
    MethodNotAllowed            = 405,
    RequestTimeout              = 408,
    LengthRequired              = 411,
    PreconditionFailed          = 412,
    ContentTooLarge             = 413,
    UriTooLong                  = 414,
    RangeNotSatisfiable         = 416,  // reserved for Range; not emitted yet
    RequestHeaderFieldsTooLarge = 431,
    InternalServerError         = 500,
    NotImplemented              = 501,
    ServiceUnavailable          = 503,
    HttpVersionNotSupported     = 505,
};

constexpr int code(Status s) noexcept { return static_cast<int>(s); }

/// The reason phrase, without the code. Returns a view into static storage.
///
/// Note 413 is "Content Too Large" and 431 is "Request Header Fields Too
/// Large": 413 is about the body, 431 about the head. Sending 413 for an
/// oversized header tells a client to shrink a body it never sent.
constexpr std::string_view reason(Status s) noexcept {
    switch (s) {
        case Status::Ok:                          return "OK";
        case Status::Created:                     return "Created";
        case Status::NoContent:                   return "No Content";
        case Status::PartialContent:              return "Partial Content";
        case Status::NotModified:                 return "Not Modified";
        case Status::BadRequest:                  return "Bad Request";
        case Status::NotFound:                    return "Not Found";
        case Status::MethodNotAllowed:            return "Method Not Allowed";
        case Status::RequestTimeout:              return "Request Timeout";
        case Status::LengthRequired:              return "Length Required";
        case Status::PreconditionFailed:          return "Precondition Failed";
        case Status::ContentTooLarge:             return "Content Too Large";
        case Status::UriTooLong:                  return "URI Too Long";
        case Status::RangeNotSatisfiable:         return "Range Not Satisfiable";
        case Status::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
        case Status::InternalServerError:         return "Internal Server Error";
        case Status::NotImplemented:              return "Not Implemented";
        case Status::ServiceUnavailable:          return "Service Unavailable";
        case Status::HttpVersionNotSupported:     return "HTTP Version Not Supported";
    }
    return "Unknown";
}

/// A short, stable, machine-readable tag for the JSON error body's "code"
/// field. Clients branch on this; never change a spelling once shipped.
constexpr std::string_view errorCode(Status s) noexcept {
    switch (s) {
        case Status::BadRequest:                  return "bad_request";
        case Status::NotFound:                    return "not_found";
        case Status::MethodNotAllowed:            return "method_not_allowed";
        case Status::RequestTimeout:              return "request_timeout";
        case Status::LengthRequired:              return "length_required";
        case Status::PreconditionFailed:          return "precondition_failed";
        case Status::ContentTooLarge:             return "content_too_large";
        case Status::UriTooLong:                  return "uri_too_long";
        case Status::RangeNotSatisfiable:         return "range_not_satisfiable";
        case Status::RequestHeaderFieldsTooLarge: return "headers_too_large";
        case Status::InternalServerError:         return "internal_error";
        case Status::NotImplemented:              return "unsupported";
        case Status::ServiceUnavailable:          return "unavailable";
        case Status::HttpVersionNotSupported:     return "bad_version";
        default:                                  return "ok";
    }
}

}  // namespace swiftsearch::http
