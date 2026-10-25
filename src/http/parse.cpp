#include "parse.hpp"

#include <limits>

namespace swiftsearch::http {
namespace {

constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

/// RFC 9110 tchar. A field name outside this set is not a field name, and
/// accepting one would let a header called "Content-Length " (with a space)
/// shadow the real thing.
constexpr bool isTokenChar(unsigned char c) noexcept {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

constexpr bool isOws(char c) noexcept { return c == ' ' || c == '\t'; }

std::string_view trimOws(std::string_view s) noexcept {
    while (!s.empty() && isOws(s.front())) s.remove_prefix(1);
    while (!s.empty() && isOws(s.back())) s.remove_suffix(1);
    return s;
}

std::string_view asView(std::span<const std::uint8_t> s) noexcept {
    return std::string_view(reinterpret_cast<const char*>(s.data()), s.size());
}

std::unexpected<ParseError> err(ParseStatus s, std::size_t at = 0) {
    return std::unexpected(ParseError{s, at});
}

Method methodOf(std::string_view t) noexcept {
    // Case-sensitive on purpose: HTTP methods are, and "get" is not GET.
    if (t == "GET") return Method::Get;
    if (t == "HEAD") return Method::Head;
    if (t == "PUT") return Method::Put;
    if (t == "DELETE") return Method::Delete;
    return Method::Unknown;
}

/// One line of the head, as a view with its terminator removed, plus where the
/// next line starts.
struct Line {
    std::string_view text;
    std::size_t next;
};

Line lineAt(std::string_view head, std::size_t from) noexcept {
    const std::size_t nl = head.find('\n', from);
    if (nl == std::string_view::npos) {
        return Line{head.substr(from), head.size()};
    }
    std::size_t end = nl;
    if (end > from && head[end - 1] == '\r') --end;
    return Line{head.substr(from, end - from), nl + 1};
}

}  // namespace

std::string_view methodName(Method m) noexcept {
    switch (m) {
        case Method::Get: return "GET";
        case Method::Head: return "HEAD";
        case Method::Put: return "PUT";
        case Method::Delete: return "DELETE";
        case Method::Unknown: return "";
    }
    return "";
}

std::string_view describe(ParseStatus s) noexcept {
    switch (s) {
        case ParseStatus::Ok: return "ok";
        case ParseStatus::Incomplete: return "incomplete head";
        case ParseStatus::HeadTooLarge: return "head past its cap";
        case ParseStatus::TooManyHeaders: return "too many headers";
        case ParseStatus::BadRequestLine: return "malformed request line";
        case ParseStatus::BadMethod: return "method not allowed";
        case ParseStatus::BadVersion: return "unsupported HTTP version";
        case ParseStatus::BadTarget: return "malformed request target";
        case ParseStatus::BadHeader: return "malformed header line";
        case ParseStatus::ObsFold: return "obsolete line folding";
        case ParseStatus::MissingHost: return "missing Host header";
        case ParseStatus::BadContentLength: return "malformed Content-Length";
        case ParseStatus::ConflictingFraming:
            return "Content-Length and Transfer-Encoding together";
        case ParseStatus::UnsupportedTransfer: return "Transfer-Encoding not supported";
        case ParseStatus::BodyTooLarge: return "body past its cap";
    }
    return "unknown";
}

bool iEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

bool hasListToken(std::string_view list, std::string_view token) noexcept {
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t comma = list.find(',', start);
        if (comma == std::string_view::npos) comma = list.size();
        if (iEquals(trimOws(list.substr(start, comma - start)), token)) return true;
        if (comma == list.size()) break;
        start = comma + 1;
    }
    return false;
}

std::optional<std::string_view> RequestHead::header(std::string_view name) const noexcept {
    for (const HeaderRef& h : headers) {
        if (iEquals(h.name, name)) return h.value;
    }
    return std::nullopt;
}

std::optional<std::size_t> findHeadEnd(std::span<const std::uint8_t> in) noexcept {
    const std::string_view s = asView(in);
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') continue;
        std::size_t end = i;
        if (end > lineStart && s[end - 1] == '\r') --end;
        if (end == lineStart) return i + 1;  // an empty line ends the head
        lineStart = i + 1;
    }
    return std::nullopt;
}

std::expected<RequestHead, ParseError> parseRequestHead(
    std::span<const std::uint8_t> in, const HeadLimits& limits) {
    // 1 and 2. Incompleteness is checked BEFORE the cap, so a head arriving in
    //          small pieces is never reported as oversized. See parse.hpp.
    const auto end = findHeadEnd(in);
    if (!end) {
        if (in.size() >= limits.maxHeadBytes) {
            return err(ParseStatus::HeadTooLarge, limits.maxHeadBytes);
        }
        return err(ParseStatus::Incomplete, in.size());
    }
    if (*end > limits.maxHeadBytes) {
        return err(ParseStatus::HeadTooLarge, limits.maxHeadBytes);
    }

    const std::string_view head = asView(in).substr(0, *end);

    RequestHead h{};
    h.headLength = *end;

    // 3. Request line: METHOD SP target SP HTTP/1.x, with exactly one space on
    //    each side. A target never contains a raw space (it would be %20), so
    //    anything else here is malformed rather than ambiguous.
    const Line first = lineAt(head, 0);
    const std::string_view rl = first.text;
    const std::size_t sp1 = rl.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) {
        return err(ParseStatus::BadRequestLine, 0);
    }
    const std::size_t sp2 = rl.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) {
        return err(ParseStatus::BadRequestLine, sp1);
    }
    if (rl.find(' ', sp2 + 1) != std::string_view::npos) {
        return err(ParseStatus::BadRequestLine, sp2);
    }

    const std::string_view methodTok = rl.substr(0, sp1);
    for (char c : methodTok) {
        if (!isTokenChar(static_cast<unsigned char>(c))) {
            return err(ParseStatus::BadRequestLine, 0);
        }
    }

    h.target = rl.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = rl.substr(sp2 + 1);

    // Version before method, so an HTTP/2 preface gets 505 rather than 405.
    if (version.size() != 8 || version.substr(0, 7) != "HTTP/1." ||
        version[7] < '0' || version[7] > '9') {
        return err(ParseStatus::BadVersion, sp2 + 1);
    }
    h.minorVersion = version[7] - '0';
    if (h.minorVersion != 0 && h.minorVersion != 1) {
        return err(ParseStatus::BadVersion, sp2 + 1);
    }

    // Origin-form only. absolute-form is for proxies and authority-form is for
    // CONNECT; this server is neither, and accepting them would mean deciding
    // whose authority the path belongs to.
    if (h.target.empty() || h.target.front() != '/') {
        return err(ParseStatus::BadTarget, sp1 + 1);
    }

    h.method = methodOf(methodTok);
    if (h.method == Method::Unknown) {
        return err(ParseStatus::BadMethod, 0);
    }

    // 4. Header lines.
    std::size_t contentLengthCount = 0;
    std::uint64_t contentLength = 0;
    bool sawTransferEncoding = false;
    bool sawHost = false;
    std::string_view connectionValue;
    std::string_view expectValue;

    for (std::size_t pos = first.next; pos < head.size();) {
        const Line line = lineAt(head, pos);
        pos = line.next;
        if (line.text.empty()) break;  // the blank line that ends the head

        // obs-fold is deprecated and unfolding it is a smuggling vector: the
        // folded value a proxy sees can differ from the one this parser sees.
        if (isOws(line.text.front())) {
            return err(ParseStatus::ObsFold, 0);
        }
        if (h.headers.size() >= limits.maxHeaders) {
            return err(ParseStatus::TooManyHeaders, 0);
        }

        const std::size_t colon = line.text.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return err(ParseStatus::BadHeader, 0);
        }
        const std::string_view name = line.text.substr(0, colon);
        for (char c : name) {
            if (!isTokenChar(static_cast<unsigned char>(c))) {
                return err(ParseStatus::BadHeader, 0);
            }
        }
        const std::string_view value = trimOws(line.text.substr(colon + 1));
        h.headers.push_back(HeaderRef{name, value});

        if (iEquals(name, "content-length")) {
            ++contentLengthCount;
            std::uint64_t parsed = 0;
            if (value.empty()) return err(ParseStatus::BadContentLength, 0);
            for (char c : value) {
                const unsigned char d = static_cast<unsigned char>(c);
                if (d < '0' || d > '9') return err(ParseStatus::BadContentLength, 0);
                const std::uint64_t digit = static_cast<std::uint64_t>(d - '0');
                if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                    return err(ParseStatus::BadContentLength, 0);
                }
                parsed = parsed * 10 + digit;
            }
            // Duplicates are legal only when they agree. Disagreeing ones are
            // the smuggling case and are refused rather than resolved.
            if (contentLengthCount > 1 && parsed != contentLength) {
                return err(ParseStatus::BadContentLength, 0);
            }
            contentLength = parsed;
        } else if (iEquals(name, "transfer-encoding")) {
            sawTransferEncoding = true;
        } else if (iEquals(name, "host")) {
            sawHost = true;
        } else if (iEquals(name, "connection")) {
            connectionValue = value;
        } else if (iEquals(name, "expect")) {
            expectValue = value;
        }
    }

    // 5. Framing. Both present is refused outright; see parse.hpp.
    if (sawTransferEncoding && contentLengthCount > 0) {
        return err(ParseStatus::ConflictingFraming, 0);
    }
    if (sawTransferEncoding) {
        return err(ParseStatus::UnsupportedTransfer, 0);
    }

    // 6. The cap, before the caller reserves a single byte for a body.
    if (contentLengthCount > 0) {
        if (contentLength > limits.maxBodyBytes) {
            return err(ParseStatus::BodyTooLarge, 0);
        }
        h.hasContentLength = true;
        h.contentLength = contentLength;
    }

    // HTTP/1.1 requires Host. Checked after framing so a smuggling attempt is
    // reported as what it is rather than as a missing header.
    if (h.minorVersion >= 1 && !sawHost) {
        return err(ParseStatus::MissingHost, 0);
    }

    // 7. Connection semantics. 1.1 is keep-alive by default, 1.0 is not.
    h.keepAlive = (h.minorVersion >= 1);
    if (!connectionValue.empty()) {
        if (hasListToken(connectionValue, "close")) {
            h.keepAlive = false;
        } else if (hasListToken(connectionValue, "keep-alive")) {
            h.keepAlive = true;
        }
    }
    h.expectContinue = hasListToken(expectValue, "100-continue");

    return h;
}

}  // namespace swiftsearch::http
