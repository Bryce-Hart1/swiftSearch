#include "url.hpp"

#include <limits>

#include "record.hpp"

namespace swiftsearch::http {
namespace {

constexpr int hexDigit(unsigned char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// A byte no key may contain. Control bytes would make a key unprintable in a
/// log and unquotable in JSON, and 0x00 in particular would truncate the key
/// anywhere it met a C string.
constexpr bool isControl(unsigned char c) noexcept { return c < 0x20 || c == 0x7F; }

/// True when `s` contains a '.' or '..' segment. Splitting is on the separator,
/// so this sees "a/./b", "./a", "a/.." and a bare ".." alike.
bool hasDotSegment(std::string_view s) noexcept {
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t slash = s.find(keys::SEPARATOR, start);
        const std::size_t end = (slash == std::string_view::npos) ? s.size() : slash;
        const std::string_view seg = s.substr(start, end - start);
        if (seg == "." || seg == "..") return true;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return false;
}

}  // namespace

std::string_view describe(UrlStatus s) noexcept {
    switch (s) {
        case UrlStatus::Ok:               return "ok";
        case UrlStatus::NoPrefixMatch:    return "path is not under this route";
        case UrlStatus::Empty:            return "empty key";
        case UrlStatus::BadPercent:       return "malformed percent escape";
        case UrlStatus::EncodedSeparator: return "encoded separator (%2F) in key";
        case UrlStatus::ControlByte:      return "control byte in key";
        case UrlStatus::DotSegment:       return "'.' or '..' segment in key";
        case UrlStatus::TooLong:          return "key past its cap";
        case UrlStatus::NotAPath:         return "not a valid key path";
        case UrlStatus::BadUtf8:          return "key is not valid UTF-8";
    }
    return "unknown";
}

bool isValidUtf8(std::string_view s) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        if (c < 0x80) {  // ASCII
            ++i;
            continue;
        }
        // width, and the lowest code point this width is allowed to encode.
        // The minimum is what rejects an overlong form: 0xC0 0x80 decodes to
        // U+0000, which must be one byte, and every decoder that accepts it
        // disagrees with every decoder that does not.
        std::size_t width = 0;
        std::uint32_t cp = 0;
        std::uint32_t min = 0;
        if ((c & 0xE0) == 0xC0) { width = 2; cp = c & 0x1Fu; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { width = 3; cp = c & 0x0Fu; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { width = 4; cp = c & 0x07u; min = 0x10000; }
        else return false;  // a lone continuation byte, or a 5+ byte lead

        if (i + width > n) return false;  // truncated sequence at the end
        for (std::size_t k = 1; k < width; ++k) {
            const unsigned char cont = p[i + k];
            if ((cont & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        if (cp < min) return false;                        // overlong
        if (cp > 0x10FFFF) return false;                   // past Unicode
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;    // surrogate half
        i += width;
    }
    return true;
}

Target splitTarget(std::string_view target) noexcept {
    const std::size_t q = target.find('?');
    if (q == std::string_view::npos) return Target{target, {}};
    return Target{target.substr(0, q), target.substr(q + 1)};
}

std::expected<std::string, UrlStatus> percentDecode(std::string_view in) {
    std::string out;
    out.reserve(in.size());  // decoding only ever shrinks
    for (std::size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c != '%') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        // Checked before either digit is read, so a '%' at the very end of the
        // input cannot walk off it.
        if (i + 2 >= in.size()) return std::unexpected(UrlStatus::BadPercent);
        const int hi = hexDigit(static_cast<unsigned char>(in[i + 1]));
        const int lo = hexDigit(static_cast<unsigned char>(in[i + 2]));
        if (hi < 0 || lo < 0) return std::unexpected(UrlStatus::BadPercent);
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return out;
}

std::expected<std::string, UrlStatus> keyFromPath(std::string_view path,
                                                  std::string_view prefix) {
    if (path.size() < prefix.size() || path.substr(0, prefix.size()) != prefix) {
        return std::unexpected(UrlStatus::NoPrefixMatch);
    }
    const std::string_view raw = path.substr(prefix.size());
    if (raw.empty()) return std::unexpected(UrlStatus::Empty);

    // Refused on the RAW bytes, before decoding. Doing it after would mean
    // validating one string and storing another. See url.hpp.
    // i + 2 < size keeps i+2 a valid index, so a trailing "%2" cannot read off
    // the end; it is caught as BadPercent by the decode below.
    for (std::size_t i = 0; i + 2 < raw.size(); ++i) {
        if (raw[i] == '%' && raw[i + 1] == '2' &&
            (raw[i + 2] == 'F' || raw[i + 2] == 'f')) {
            return std::unexpected(UrlStatus::EncodedSeparator);
        }
    }

    auto decoded = percentDecode(raw);
    if (!decoded) return std::unexpected(decoded.error());
    std::string key = std::move(*decoded);

    if (key.empty()) return std::unexpected(UrlStatus::Empty);
    // The cap is checked before the shape, so an enormous key reports its real
    // problem rather than whatever its first bad segment happens to be.
    if (key.size() > MAX_KEY_LEN) return std::unexpected(UrlStatus::TooLong);
    for (char ch : key) {
        if (isControl(static_cast<unsigned char>(ch))) {
            return std::unexpected(UrlStatus::ControlByte);
        }
    }
    if (hasDotSegment(key)) return std::unexpected(UrlStatus::DotSegment);
    // Checked here rather than in the JSON writer: a key that cannot be spelled
    // in JSON must never reach the store, or every listing containing it is
    // unparseable. See url.hpp.
    if (!isValidUtf8(key)) return std::unexpected(UrlStatus::BadUtf8);
    if (!keys::is_valid(key)) return std::unexpected(UrlStatus::NotAPath);
    return key;
}

std::optional<std::string_view> queryParam(std::string_view query,
                                           std::string_view name) noexcept {
    std::size_t start = 0;
    while (start < query.size()) {
        std::size_t amp = query.find('&', start);
        if (amp == std::string_view::npos) amp = query.size();
        const std::string_view pair = query.substr(start, amp - start);
        const std::size_t eq = pair.find('=');
        const std::string_view k = (eq == std::string_view::npos) ? pair : pair.substr(0, eq);
        if (k == name) {
            if (eq == std::string_view::npos) return std::string_view{};
            return pair.substr(eq + 1);
        }
        start = amp + 1;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> parseDecimal(std::string_view s) noexcept {
    if (s.empty()) return std::nullopt;
    std::uint64_t v = 0;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < '0' || c > '9') return std::nullopt;
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        // Checked before the multiply, not after: detecting an overflow by
        // noticing the result got smaller is undefined for signed types and
        // merely wrong for unsigned ones.
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}

}  // namespace swiftsearch::http
