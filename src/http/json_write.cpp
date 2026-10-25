#include "json_write.hpp"

namespace swiftsearch::http {
namespace {

constexpr std::uint32_t MAX_DEPTH = 32;  // the bitmask's width

void appendU64(std::string& out, std::uint64_t v) {
    char buf[20];
    std::size_t n = 0;
    do {
        buf[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    while (n > 0) out.push_back(buf[--n]);
}

}  // namespace

void escapeTo(std::string& out, std::string_view s) {
    static constexpr char HEX[] = "0123456789abcdef";
    out.push_back('"');
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            default: break;
        }
        if (c < 0x20) {
            // The only remaining legal escape for a control byte. Note \u00XX
            // is correct here precisely because c < 0x20 is also its own code
            // point; this would be wrong for a byte >= 0x80, which is why those
            // are passed through instead. See json_write.hpp.
            out += "\\u00";
            out.push_back(HEX[c >> 4]);
            out.push_back(HEX[c & 0x0F]);
            continue;
        }
        out.push_back(ch);
    }
    out.push_back('"');
}

void Json::sep() {
    if (_afterKey) {
        _afterKey = false;
        return;
    }
    if (_depth > 0 && _depth <= MAX_DEPTH) {
        const std::uint32_t bit = 1u << (_depth - 1);
        if (_nonEmpty & bit) _out.push_back(',');
        _nonEmpty |= bit;
    }
}

Json& Json::obj() {
    sep();
    _out.push_back('{');
    if (_depth < MAX_DEPTH) {
        ++_depth;
        _nonEmpty &= ~(1u << (_depth - 1));  // a fresh container is empty
    }
    return *this;
}

Json& Json::endObj() {
    _out.push_back('}');
    if (_depth > 0) --_depth;
    return *this;
}

Json& Json::arr() {
    sep();
    _out.push_back('[');
    if (_depth < MAX_DEPTH) {
        ++_depth;
        _nonEmpty &= ~(1u << (_depth - 1));
    }
    return *this;
}

Json& Json::endArr() {
    _out.push_back(']');
    if (_depth > 0) --_depth;
    return *this;
}

Json& Json::key(std::string_view name) {
    sep();
    escapeTo(_out, name);
    _out.push_back(':');
    _afterKey = true;
    return *this;
}

Json& Json::val(std::string_view s) {
    sep();
    escapeTo(_out, s);
    return *this;
}

Json& Json::val(std::uint64_t v) {
    sep();
    appendU64(_out, v);
    return *this;
}

Json& Json::val(std::int64_t v) {
    sep();
    if (v < 0) {
        _out.push_back('-');
        // Negated as unsigned so the most negative int64 does not overflow,
        // which it would as -v in signed arithmetic.
        appendU64(_out, ~static_cast<std::uint64_t>(v) + 1u);
    } else {
        appendU64(_out, static_cast<std::uint64_t>(v));
    }
    return *this;
}

Json& Json::val(bool b) {
    sep();
    _out += b ? "true" : "false";
    return *this;
}

Json& Json::raw(std::string_view fragment) {
    sep();
    _out.append(fragment);
    return *this;
}

}  // namespace swiftsearch::http
