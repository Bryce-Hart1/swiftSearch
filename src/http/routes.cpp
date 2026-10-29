#include "routes.hpp"

#include <utility>

#include "conditional.hpp"
#include "json_write.hpp"
#include "mime.hpp"
#include "record.hpp"
#include "url.hpp"

namespace swiftsearch::http {
namespace {

constexpr std::string_view ALLOW_BLOBS = "GET, HEAD, PUT, DELETE";
constexpr std::string_view ALLOW_BOXES = "GET, PUT";
constexpr std::string_view ALLOW_PREPARE = "POST";

/// Every way a URL can fail to be a key, mapped once. 414 is distinguished from
/// 400 because "your key is too long" is actionable and "bad request" is not.
Status statusForUrl(UrlStatus s) noexcept {
    switch (s) {
        case UrlStatus::TooLong: return Status::UriTooLong;
        case UrlStatus::Ok: return Status::Ok;
        default: return Status::BadRequest;
    }
}

Response notImplemented(std::string_view what) {
    // An unbound function in the StoreBinding. 501 rather than 500: the server
    // is working correctly, this route simply has nothing behind it yet.
    std::string msg = "not implemented: ";
    msg.append(what);
    return Response::error(Status::NotImplemented, msg);
}

void putHashHeader(Response& r, const std::optional<Digest>& d) {
    if (d) r.set(std::string(H_HASH), to_hex(*d));
}

/// The JSON body shared by both PUT routes.
std::string storedJson(std::string_view key, const Stored& s, VAR_TYPE t) {
    std::string out;
    Json j(out);
    j.obj().field("key", key);
    if (s.digest) {
        j.field("hash", std::string_view(to_hex(*s.digest)));
    }
    j.field("size", s.size)
        .field("type", var_type_name(t))
        .field("created", s.created)
        .field("deduped", s.deduped)
        .endObj();
    return out;
}

// --- blobs -----------------------------------------------------------------

Response getBlob(const Request& req, const StoreBinding& store, std::string_view key) {
    if (!store.get) return notImplemented("get");

    auto got = store.get(key);
    if (!got) {
        // A corrupt record never puts its payload in a response body. Section 5
        // of the design notes: detect, refuse, report.
        return Response::error(statusFor(got.error()), describe(got.error()));
    }

    // HEAD keeps every header, including Content-Length, and drops only the
    // bytes. That is what makes `curl -I` a usable way to inspect a blob, and
    // it applies to a 206 just as much as to a 200.
    const bool headOnly = (req.method == Method::Head);
    const std::uint64_t size = got->bytes.size();
    const std::string etag = got->digest ? etagOf(*got->digest) : std::string{};

    // On every answer below -- 200, 206, 304 and 416 alike -- because a client
    // needs the validator and the range capability whichever one it got.
    const auto addValidators = [&etag](Response& out) {
        out.set("Accept-Ranges", "bytes");
        if (!etag.empty()) out.set("ETag", etag);
        // Deliberately NOT `immutable` or a long max-age, which is where
        // section 11 of the design notes is half right. "Blobs are immutable,
        // so cache aggressively" is true of a HASH-addressed URL and false of
        // this one: a PUT replaces what a key points at, so a cached copy can
        // absolutely go stale. `no-cache` means store it but revalidate, and
        // the ETag is what makes that revalidation cost a bodyless 304.
        out.set("Cache-Control", "no-cache");
    };

    // 1. The conditional check comes FIRST and wins: a client that already
    //    holds these bytes has no use for a slice of them.
    if (!etag.empty()) {
        if (const auto inm = req.header("if-none-match")) {
            if (ifNoneMatchMatches(*inm, etag)) {
                Response nm = Response::empty(Status::NotModified);
                addValidators(nm);
                // No Content-Type and no body: a 304 says "what you have is
                // still good", so repeating the representation's own headers
                // would be noise the client already has.
                return nm;
            }
        }
    }

    Response r = Response::empty(Status::Ok);
    r.set("Content-Type", std::string(contentTypeFor(got->var_type, key)));
    r.set(std::string(H_TYPE), std::string(var_type_name(got->var_type)));
    putHashHeader(r, got->digest);
    addValidators(r);

    // 2. If-Range gates the Range, then the Range itself.
    if (const auto rangeHeader = req.header("range")) {
        bool honour = true;
        if (const auto ifRange = req.header("if-range")) {
            // "Send me the rest of what I was downloading, but only if it has
            // not changed." A mismatch means the whole representation, never a
            // slice of different content spliced onto the client's partial file.
            honour = !etag.empty() && ifRangeMatches(*ifRange, etag);
        }
        if (honour) {
            const RangeResult rr = parseRange(*rangeHeader, size);
            if (rr.status == RangeStatus::Unsatisfiable) {
                Response e = Response::error(Status::RangeNotSatisfiable,
                                             describe(RangeStatus::Unsatisfiable));
                addValidators(e);
                // The size is the point of this header: it tells the client
                // what it should have asked for.
                e.set("Content-Range", contentRangeUnsatisfiable(size));
                return e;
            }
            if (rr.status == RangeStatus::Ok) {
                r.status = Status::PartialContent;
                r.set("Content-Range", contentRange(rr.range, size));
                const auto begin = got->bytes.begin();
                r.body.assign(begin + static_cast<std::ptrdiff_t>(rr.range.first),
                              begin + static_cast<std::ptrdiff_t>(rr.range.last) + 1);
                r.omitBody = headOnly;
                return r;
            }
            // Ignored: a malformed or multi-range header falls through to the
            // full 200, which is what RFC 9110 requires.
        }
    }

    r.body = std::move(got->bytes);
    r.omitBody = headOnly;
    return r;
}

Response putBlob(const Request& req, const StoreBinding& store, std::string_view key,
                 const RouteLimits&) {
    if (!store.put) return notImplemented("put");

    // Without a Content-Length there is no frame, so the server cannot know
    // where this body ends. Fatal because unread bytes may still be queued:
    // the next thing on the socket is not a request.
    if (!req.header("content-length").has_value()) {
        return Response::fatal(Status::LengthRequired,
                               "PUT requires Content-Length");
    }

    // The explicit header wins; an unrecognised name is refused rather than
    // quietly falling back to a type the client did not ask for.
    VAR_TYPE vt;
    if (const auto declared = req.header(H_TYPE)) {
        const auto parsed = var_type_from_name(*declared);
        if (!parsed) {
            std::string msg = "unknown X-SwiftSearch-Type: ";
            msg.append(*declared);
            return Response::error(Status::BadRequest, msg);
        }
        vt = *parsed;
    } else {
        vt = inferVarType(req.header("content-type").value_or(""), key);
    }

    auto stored = store.put(key, req.body, vt);
    if (!stored) {
        return Response::error(statusFor(stored.error()), describe(stored.error()));
    }

    Response r = Response::json(stored->created ? Status::Created : Status::Ok,
                                storedJson(key, *stored, vt));
    if (stored->created) {
        std::string loc(BLOBS_PREFIX);
        loc.append(key);
        r.set("Location", std::move(loc));
    }
    putHashHeader(r, stored->digest);
    return r;
}

Response deleteBlob(const StoreBinding& store, std::string_view key) {
    if (!store.remove) return notImplemented("delete");
    auto gone = store.remove(key);
    if (!gone) return Response::error(statusFor(gone.error()), describe(gone.error()));
    if (!*gone) return Response::error(Status::NotFound, "no such key");
    return Response::empty(Status::NoContent);
}

Response prepareBlob(const StoreBinding& store, std::string_view key) {
    if (!store.prepare) return notImplemented("prepare");
    auto done = store.prepare(key);
    if (!done) return Response::error(statusFor(done.error()), describe(done.error()));
    return Response::empty(Status::NoContent);
}

// --- boxes -----------------------------------------------------------------

Response getBox(const StoreBinding& store, std::string_view key,
                std::string_view query, const RouteLimits& limits) {
    if (!store.list) return notImplemented("list");

    ListQuery q;
    q.prefix.assign(key);
    q.limit = limits.defaultPageLimit;

    if (const auto raw = queryParam(query, "limit")) {
        const auto n = parseDecimal(*raw);
        // Rejected rather than clamped: a client that asked for "abc" items has
        // a bug, and silently giving it 100 hides that.
        if (!n || *n == 0) return Response::error(Status::BadRequest, "bad ?limit");
        q.limit = static_cast<std::size_t>(
            *n > limits.maxPageLimit ? limits.maxPageLimit : *n);
    }
    if (const auto raw = queryParam(query, "cursor")) {
        auto decoded = percentDecode(*raw);
        if (!decoded) return Response::error(Status::BadRequest, "bad ?cursor");
        q.cursor = std::move(*decoded);
    }
    q.deep = true;
    if (const auto raw = queryParam(query, "depth")) {
        if (*raw != "1") {
            return Response::error(Status::BadRequest, "?depth accepts only 1");
        }
        q.deep = false;
    }

    auto listed = store.list(q);
    if (!listed) return Response::error(statusFor(listed.error()), describe(listed.error()));

    std::string out;
    Json j(out);
    j.obj().field("key", key);
    if (listed->meta) {
        j.field("display_name", std::string_view(listed->meta->display_name))
            .field("created_ns", listed->meta->created_ns)
            .key("tags")
            .arr();
        for (const std::string& t : listed->meta->tags) j.val(std::string_view(t));
        j.endArr();
    }
    j.key("children").arr();
    for (const Child& c : listed->children) {
        j.obj().field("key", std::string_view(c.key)).field("size", c.size);
        if (c.digest) j.field("hash", std::string_view(to_hex(*c.digest)));
        j.field("type", var_type_name(c.var_type))
            .field("timestamp_ns", c.timestamp_ns)
            .endObj();
    }
    j.endArr();
    // Present only when there is a next page, so a client's loop condition is
    // "is there a cursor" rather than "did I get fewer than I asked for" --
    // which is wrong whenever a page lands exactly on the boundary.
    if (!listed->next_cursor.empty()) {
        j.field("next_cursor", std::string_view(listed->next_cursor));
    }
    j.endObj();
    return Response::json(Status::Ok, std::move(out));
}

Response putBox(const Request& req, const StoreBinding& store, std::string_view key,
                const RouteLimits& limits) {
    if (!store.putBox) return notImplemented("putBox");

    BoxMeta meta;
    meta.created_ns = nowNanos();

    // Percent-decoded so a display name can hold non-ASCII while the header
    // itself stays ASCII, and UTF-8-checked for the same reason keys are: a
    // name that cannot be written into JSON must not reach the store.
    if (const auto raw = req.header(H_DISPLAY_NAME)) {
        auto decoded = percentDecode(*raw);
        if (!decoded) {
            return Response::error(Status::BadRequest, "bad X-SwiftSearch-Display-Name");
        }
        if (!isValidUtf8(*decoded)) {
            return Response::error(Status::BadRequest,
                                   "X-SwiftSearch-Display-Name is not valid UTF-8");
        }
        if (decoded->size() > MAX_BOX_NAME_LEN) {
            return Response::error(Status::BadRequest,
                                   "X-SwiftSearch-Display-Name too long");
        }
        meta.display_name = std::move(*decoded);
    }

    if (const auto raw = req.header(H_TAGS)) {
        std::string_view list = *raw;
        std::size_t start = 0;
        while (start <= list.size()) {
            std::size_t comma = list.find(',', start);
            if (comma == std::string_view::npos) comma = list.size();
            std::string_view piece = list.substr(start, comma - start);
            while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) {
                piece.remove_prefix(1);
            }
            while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t')) {
                piece.remove_suffix(1);
            }
            if (!piece.empty()) {
                if (meta.tags.size() >= limits.maxTags) {
                    return Response::error(Status::BadRequest, "too many tags");
                }
                auto decoded = percentDecode(piece);
                if (!decoded) return Response::error(Status::BadRequest, "bad tag");
                if (!isValidUtf8(*decoded)) {
                    return Response::error(Status::BadRequest, "tag is not valid UTF-8");
                }
                if (decoded->size() > MAX_BOX_NAME_LEN) {
                    return Response::error(Status::BadRequest, "tag too long");
                }
                meta.tags.push_back(std::move(*decoded));
            }
            if (comma == list.size()) break;
            start = comma + 1;
        }
    }

    auto stored = store.putBox(key, meta);
    if (!stored) return Response::error(statusFor(stored.error()), describe(stored.error()));

    Response r = Response::json(stored->created ? Status::Created : Status::Ok,
                                storedJson(key, *stored, VAR_TYPE::UNSET));
    if (stored->created) {
        std::string loc(BOXES_PREFIX);
        loc.append(key);
        r.set("Location", std::move(loc));
    }
    return r;
}

Response methodNotAllowed(std::string_view allow) {
    Response r = Response::error(Status::MethodNotAllowed, "method not allowed here");
    r.set("Allow", std::string(allow));
    return r;
}

}  // namespace

std::string_view describe(StoreStatus s) noexcept {
    switch (s) {
        case StoreStatus::NotFound: return "no such key";
        case StoreStatus::Corrupt: return "stored record is damaged";
        case StoreStatus::Unsupported: return "stored record uses a feature this build lacks";
        case StoreStatus::Internal: return "internal store error";
        case StoreStatus::TooLarge: return "value is too large for this operation";
    }
    return "unknown";
}

Status statusFor(StoreStatus s) noexcept {
    switch (s) {
        case StoreStatus::NotFound: return Status::NotFound;
        // 500, not 404: the key exists and the server cannot honour it. A 404
        // would tell the client to forget about data that is still there and
        // still needs restoring from a replica.
        case StoreStatus::Corrupt: return Status::InternalServerError;
        case StoreStatus::Unsupported: return Status::NotImplemented;
        case StoreStatus::Internal: return Status::InternalServerError;
        case StoreStatus::TooLarge: return Status::ContentTooLarge;
    }
    return Status::InternalServerError;
}

Response route(const Request& req, const StoreBinding& store,
               const RouteLimits& limits) {
    const Target t = splitTarget(req.target);

    if (t.path == HEALTH_PATH) {
        if (req.method != Method::Get && req.method != Method::Head) {
            return methodNotAllowed("GET, HEAD");
        }
        Response r = Response::json(Status::Ok, "{\"status\":\"ok\"}");
        r.omitBody = (req.method == Method::Head);
        return r;
    }

    const bool isBlob = t.path.starts_with(BLOBS_PREFIX);
    const bool isBox = t.path.starts_with(BOXES_PREFIX);
    const bool isPrepare = t.path.starts_with(PREPARE_PREFIX);
    if (!isBlob && !isBox && !isPrepare) {
        return Response::error(Status::NotFound, "no such route");
    }

    const std::string_view prefix = isBlob ? BLOBS_PREFIX
                                  : isBox  ? BOXES_PREFIX
                                           : PREPARE_PREFIX;
    auto key = keyFromPath(t.path, prefix);
    if (!key) {
        return Response::error(statusForUrl(key.error()), describe(key.error()));
    }

    if (isPrepare) {
        if (req.method != Method::Post) return methodNotAllowed(ALLOW_PREPARE);
        return prepareBlob(store, *key);
    }

    if (isBlob) {
        switch (req.method) {
            case Method::Get:
            case Method::Head: return getBlob(req, store, *key);
            case Method::Put: return putBlob(req, store, *key, limits);
            case Method::Delete: return deleteBlob(store, *key);
            case Method::Post:
            case Method::Unknown: break;
        }
        return methodNotAllowed(ALLOW_BLOBS);
    }

    switch (req.method) {
        case Method::Get: return getBox(store, *key, t.query, limits);
        case Method::Put: return putBox(req, store, *key, limits);
        default: return methodNotAllowed(ALLOW_BOXES);
    }
}

}  // namespace swiftsearch::http
