#pragma once
/**
 * The dock itself: a request and a store binding in, a response out.
 *
 * Pure, in the sense that matters: no sockets, no threads, no clock, no global
 * state. That is what lets every route be tested against a lambda in a test
 * file, with no network and no storage engine -- the same separation record.hpp
 * draws between a format and the file it lives in.
 *
 * ROUTES
 *   GET    /v1/blobs/<key>     the payload, as the body, unwrapped
 *   HEAD   /v1/blobs/<key>     the same headers, no body
 *   PUT    /v1/blobs/<key>     the body IS the payload; metadata in headers
 *   DELETE /v1/blobs/<key>     204, or 404 if absent
 *   GET    /v1/boxes/<key>     JSON: box metadata plus one page of children
 *   PUT    /v1/boxes/<key>     box metadata, from headers
 *   POST   /v1/prepare/<key>   load the value into memory; 204, 404, or 413
 *   GET    /healthz            liveness, for a supervisor
 */

#include <cstddef>

#include "message.hpp"
#include "store_binding.hpp"

namespace swiftsearch::http {

inline constexpr std::string_view BLOBS_PREFIX = "/v1/blobs/";
inline constexpr std::string_view BOXES_PREFIX = "/v1/boxes/";
inline constexpr std::string_view PREPARE_PREFIX = "/v1/prepare/";
inline constexpr std::string_view HEALTH_PATH = "/healthz";

/// Header names, spelled once. A typo in one of these is a header silently
/// ignored, which is the hardest kind of mistake to see in a response.
inline constexpr std::string_view H_TYPE = "X-SwiftSearch-Type";
inline constexpr std::string_view H_HASH = "X-SwiftSearch-Hash";
inline constexpr std::string_view H_DISPLAY_NAME = "X-SwiftSearch-Display-Name";
inline constexpr std::string_view H_TAGS = "X-SwiftSearch-Tags";

struct RouteLimits {
    std::size_t defaultPageLimit = 100;
    std::size_t maxPageLimit = 1000;
    /// Caps what a single box record's metadata may carry, independently of
    /// what BoxMeta::encode would accept, because these arrive in headers.
    std::size_t maxTags = 64;
};

Response route(const Request& req, const StoreBinding& store,
               const RouteLimits& limits = {});

/// Exposed for testing and for the reactor's early-rejection paths, which have
/// to answer before a route is ever chosen.
Status statusFor(StoreStatus s) noexcept;

}  // namespace swiftsearch::http
