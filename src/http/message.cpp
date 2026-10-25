#include "message.hpp"

#include "json_write.hpp"

namespace swiftsearch::http {

Request Request::own(const RequestHead& head, Bytes body) {
    Request r;
    r.method = head.method;
    r.target.assign(head.target);
    r.minorVersion = head.minorVersion;
    r.keepAlive = head.keepAlive;
    r.expectContinue = head.expectContinue;
    r.headers.reserve(head.headers.size());
    for (const HeaderRef& h : head.headers) {
        r.headers.emplace_back(std::string(h.name), std::string(h.value));
    }
    r.body = std::move(body);
    return r;
}

std::optional<std::string_view> Request::header(std::string_view name) const noexcept {
    for (const auto& [n, v] : headers) {
        if (iEquals(n, name)) return std::string_view(v);
    }
    return std::nullopt;
}

Response& Response::set(std::string name, std::string value) {
    headers.emplace_back(std::move(name), std::move(value));
    return *this;
}

bool Response::hasHeader(std::string_view name) const noexcept {
    for (const auto& [n, v] : headers) {
        if (iEquals(n, name)) return true;
    }
    return false;
}

Response Response::empty(Status s) {
    Response r;
    r.status = s;
    return r;
}

Response Response::json(Status s, std::string body) {
    Response r;
    r.status = s;
    r.set("Content-Type", std::string(JSON_TYPE_LITERAL));
    r.body.assign(body.begin(), body.end());
    return r;
}

Response Response::error(Status s, std::string_view message) {
    std::string out;
    Json(out).obj()
        .field("error", message)
        .field("code", errorCode(s))
        .field("status", static_cast<std::uint64_t>(code(s)))
        .endObj();
    return json(s, std::move(out));
}

Response Response::fatal(Status s, std::string_view message) {
    Response r = error(s, message);
    r.closeAfter = true;
    return r;
}

void serializeHead(const Response& r, bool keepAlive, std::string& out) {
    out += "HTTP/1.1 ";
    const int c = code(r.status);
    out.push_back(static_cast<char>('0' + (c / 100) % 10));
    out.push_back(static_cast<char>('0' + (c / 10) % 10));
    out.push_back(static_cast<char>('0' + c % 10));
    out.push_back(' ');
    out.append(reason(r.status));
    out += "\r\n";

    for (const auto& [name, value] : r.headers) {
        out.append(name);
        out += ": ";
        out.append(value);
        out += "\r\n";
    }

    if (!r.hasHeader("x-content-type-options")) {
        out += "X-Content-Type-Options: nosniff\r\n";
    }

    // Always present, even at 0: a framed response is one the client can finish
    // reading without waiting for a close.
    out += "Content-Length: ";
    std::uint64_t n = r.body.size();
    char buf[20];
    std::size_t len = 0;
    do {
        buf[len++] = static_cast<char>('0' + (n % 10));
        n /= 10;
    } while (n != 0);
    while (len > 0) out.push_back(buf[--len]);
    out += "\r\n";

    out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    out += "\r\n";
}

}  // namespace swiftsearch::http
