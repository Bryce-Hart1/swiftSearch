#pragma once
/**
 * The seam between the dock and the store.
 *
 * The store (SwiftSearch::get/put/delete) is being written separately and its
 * input types are not settled, so the dock must not dictate them. This is a
 * struct of std::functions rather than an abstract base class for exactly that
 * reason: you write your methods with whatever signatures suit the storage
 * engine, and construct a StoreBinding with lambdas that adapt them. When a
 * signature changes you change one lambda, not an interface, the dock, and
 * every test double at once.
 *
 * AN UNBOUND FUNCTION IS A 501. That is the property that lets the whole dock
 * be built and tested before the store exists, and it keeps a half-built store
 * honest: `list` answers "not implemented" until there is a prefix scan behind
 * it, rather than pretending a box is empty.
 *
 * The structs below are the dock's own vocabulary. They are deliberately plain
 * data with no behaviour, so adapting to them is a field-by-field copy and
 * never a conversion anyone has to think about.
 */

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "box_meta.hpp"
#include "bytes.hpp"
#include "sha256.hpp"
#include "var_type.hpp"

namespace swiftsearch::http {

/// Why a store call did not succeed. Mapped to HTTP in routes.cpp, in one
/// place, so no route invents its own answer.
enum class StoreStatus : std::uint8_t {
    NotFound = 0,
    /// The record is damaged. Section 5 of the design notes is absolute here:
    /// detect it, refuse to return the bytes, report it. Never serve a payload
    /// that failed its checksum.
    Corrupt,
    /// Intact bytes this build will not interpret -- a compression id or a
    /// var_type from a newer writer. Not damage, so it is a different answer.
    Unsupported,
    /// Anything else: I/O failure, a bug. Deliberately vague to a client.
    Internal,
    /// The value exists but is bigger than this operation is allowed to take
    /// on -- a prepare that would push too much of the cache out.
    TooLarge,
};

std::string_view describe(StoreStatus s) noexcept;

template <typename T>
using StoreResult = std::expected<T, StoreStatus>;

/// What a GET needs. The digest is optional because a store may not have it to
/// hand for every record; when present it is sent as X-SwiftSearch-Hash and
/// becomes the ETag once conditional requests land.
struct Fetched {
    Bytes bytes;
    VAR_TYPE var_type = VAR_TYPE::UNSET;
    std::optional<Digest> digest;
    std::int64_t timestamp_ns = 0;
};

/// What a PUT reports back.
struct Stored {
    std::optional<Digest> digest;
    std::uint64_t size = 0;
    /// True when the key did not exist before, which is the difference between
    /// 201 Created and 200 OK. A client that cares whether it overwrote
    /// something has no other way to find out.
    bool created = true;
    /// True when the payload's hash was already present, so no bytes were
    /// written and only a new key record was appended. Free to report, and the
    /// single most interesting thing a content-addressed store can tell you.
    bool deduped = false;
};

/// One entry in a box listing.
struct Child {
    std::string key;
    std::uint64_t size = 0;
    VAR_TYPE var_type = VAR_TYPE::UNSET;
    std::optional<Digest> digest;
    std::int64_t timestamp_ns = 0;
};

struct ListQuery {
    std::string prefix;
    /// Resume point: the last key of the previous page. Empty for the first.
    std::string cursor;
    std::size_t limit = 100;
    /// False: direct children only. True: every descendant.
    bool deep = true;
};

struct Listing {
    /// The box's own record, when it has one. A prefix with children but no
    /// Box-flagged record is a legitimate state (nobody created the box, they
    /// just stored keys under it), so this is optional rather than an error.
    std::optional<BoxMeta> meta;
    std::vector<Child> children;
    /// Empty when this is the last page. Otherwise the cursor for the next.
    std::string next_cursor;
};

struct StoreBinding {
    std::function<StoreResult<Fetched>(std::string_view key)> get;

    std::function<StoreResult<Stored>(std::string_view key,
                                      std::span<const std::uint8_t> payload,
                                      VAR_TYPE var_type)> put;

    /// The bool is whether the key existed, which is 204 versus 404. Appending
    /// the tombstone is the store's business; the dock does not know the word.
    std::function<StoreResult<bool>(std::string_view key)> remove;

    std::function<StoreResult<Listing>(const ListQuery& q)> list;

    std::function<StoreResult<Stored>(std::string_view key,
                                      const BoxMeta& meta)> putBox;

    /// Load a key's value into memory so the next GET is served from RAM. The
    /// value stays on disk; this only warms the cache.
    std::function<StoreResult<void>(std::string_view key)> prepare;
};

}  // namespace swiftsearch::http
