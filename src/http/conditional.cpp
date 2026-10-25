#include "conditional.hpp"

#include <limits>

#include "parse.hpp"

namespace swiftsearch::http {
namespace {

constexpr bool isOws(char c) noexcept { return c == ' ' || c == '\t'; }

std::string_view trimOws(std::string_view s) noexcept {
    while (!s.empty() && isOws(s.front())) s.remove_prefix(1);
    while (!s.empty() && isOws(s.back())) s.remove_suffix(1);
    return s;
}

/// Drops a `W/` prefix if present. Used only where weak comparison applies.
std::string_view stripWeak(std::string_view tag) noexcept {
    if (tag.size() >= 2 && (tag[0] == 'W' || tag[0] == 'w') && tag[1] == '/') {
        return tag.substr(2);
    }
    return tag;
}

bool isWeak(std::string_view tag) noexcept { return stripWeak(tag).size() != tag.size(); }

void appendU64(std::string& out, std::uint64_t v) {
    char buf[20];
    std::size_t n = 0;
    do {
        buf[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    while (n > 0) out.push_back(buf[--n]);
}

/// A full-string decimal parse. Returns false on empty, a non-digit, or
/// overflow -- the same discipline url.cpp's parseDecimal applies, for the same
/// reason: this is a number from a request header.
bool parseU64(std::string_view s, std::uint64_t& out) noexcept {
    if (s.empty()) return false;
    std::uint64_t v = 0;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < '0' || c > '9') return false;
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

}  // namespace

std::string etagOf(const Digest& d) {
    std::string out;
    out.reserve(66);
    out.push_back('"');
    out.append(to_hex(d));
    out.push_back('"');
    return out;
}

bool ifNoneMatchMatches(std::string_view header, std::string_view etag) noexcept {
    const std::string_view want = stripWeak(trimOws(etag));
    std::size_t start = 0;
    while (start <= header.size()) {
        std::size_t comma = header.find(',', start);
        if (comma == std::string_view::npos) comma = header.size();
        const std::string_view tag = trimOws(header.substr(start, comma - start));
        if (tag == "*") return true;  // matches any existing representation
        // Weak comparison: the W/ marker is ignored on BOTH sides.
        if (!tag.empty() && stripWeak(tag) == want) return true;
        if (comma == header.size()) break;
        start = comma + 1;
    }
    return false;
}

bool ifRangeMatches(std::string_view header, std::string_view etag) noexcept {
    const std::string_view given = trimOws(header);
    // Strong comparison: a weak tag on either side can never match, because a
    // weak validator permits byte-level differences and a range splice cannot.
    if (given.empty() || isWeak(given) || isWeak(etag)) return false;
    // Not a quoted tag at all (very likely an HTTP-date, which is not
    // implemented): fail, which makes the caller ignore the Range and send the
    // whole representation. The safe direction.
    if (given.front() != '"') return false;
    return given == etag;
}

std::string_view describe(RangeStatus s) noexcept {
    switch (s) {
        case RangeStatus::Absent: return "no range requested";
        case RangeStatus::Ok: return "satisfiable range";
        case RangeStatus::Unsatisfiable: return "range not satisfiable";
        case RangeStatus::Ignored: return "range ignored";
    }
    return "unknown";
}

RangeResult parseRange(std::string_view header, std::uint64_t size) noexcept {
    const std::string_view h = trimOws(header);
    if (h.empty()) return {RangeStatus::Absent, {}};

    const std::size_t eq = h.find('=');
    if (eq == std::string_view::npos) return {RangeStatus::Ignored, {}};
    // Range units are case-insensitive, and `bytes` is the only one defined.
    if (!iEquals(trimOws(h.substr(0, eq)), "bytes")) return {RangeStatus::Ignored, {}};

    const std::string_view set = trimOws(h.substr(eq + 1));
    // More than one range would need a multipart/byteranges body, which is a
    // second framing to get right for no caller here.
    if (set.find(',') != std::string_view::npos) return {RangeStatus::Ignored, {}};

    const std::size_t dash = set.find('-');
    if (dash == std::string_view::npos) return {RangeStatus::Ignored, {}};

    const std::string_view firstTok = trimOws(set.substr(0, dash));
    const std::string_view lastTok = trimOws(set.substr(dash + 1));

    // A zero-length representation can satisfy no range at all.
    if (size == 0) return {RangeStatus::Unsatisfiable, {}};

    if (firstTok.empty()) {
        // Suffix form, "-N": the LAST n bytes.
        std::uint64_t suffix = 0;
        if (!parseU64(lastTok, suffix)) return {RangeStatus::Ignored, {}};
        // "-0" asks for the last zero bytes, which is satisfiable by nothing.
        if (suffix == 0) return {RangeStatus::Unsatisfiable, {}};
        const std::uint64_t first = suffix >= size ? 0 : size - suffix;
        return {RangeStatus::Ok, ByteRange{first, size - 1}};
    }

    std::uint64_t first = 0;
    if (!parseU64(firstTok, first)) return {RangeStatus::Ignored, {}};

    if (lastTok.empty()) {
        // "N-": from N to the end.
        if (first >= size) return {RangeStatus::Unsatisfiable, {}};
        return {RangeStatus::Ok, ByteRange{first, size - 1}};
    }

    std::uint64_t last = 0;
    if (!parseU64(lastTok, last)) return {RangeStatus::Ignored, {}};
    // first > last is syntactically invalid, so it is ignored rather than 416'd.
    if (first > last) return {RangeStatus::Ignored, {}};
    if (first >= size) return {RangeStatus::Unsatisfiable, {}};
    // A last-byte-pos past the end is clamped, not an error: asking for more
    // than exists is how a client says "to the end" without knowing the size.
    if (last >= size) last = size - 1;
    return {RangeStatus::Ok, ByteRange{first, last}};
}

std::string contentRange(const ByteRange& r, std::uint64_t size) {
    std::string out = "bytes ";
    appendU64(out, r.first);
    out.push_back('-');
    appendU64(out, r.last);
    out.push_back('/');
    appendU64(out, size);
    return out;
}

std::string contentRangeUnsatisfiable(std::uint64_t size) {
    std::string out = "bytes */";
    appendU64(out, size);
    return out;
}

}  // namespace swiftsearch::http
