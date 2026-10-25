#include "mime.hpp"

#include "record.hpp"

namespace swiftsearch::http {
namespace {

constexpr std::size_t MAX_EXTENSION = 16;  // longer than any real one

constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

struct Entry {
    std::string_view ext;
    std::string_view type;
};

// Only formats a browser renders inline and cannot execute. See mime.hpp for
// why svg is absent.
constexpr Entry IMAGE_TYPES[] = {
    {"jpg", "image/jpeg"},  {"jpeg", "image/jpeg"}, {"png", "image/png"},
    {"gif", "image/gif"},   {"webp", "image/webp"}, {"avif", "image/avif"},
    {"bmp", "image/bmp"},   {"tif", "image/tiff"},  {"tiff", "image/tiff"},
    {"heic", "image/heic"}, {"heif", "image/heif"},
};

// Non-executable text types only. html, htm, xhtml, xml and svg are absent on
// purpose and fall through to text/plain.
constexpr Entry TEXT_TYPES[] = {
    {"txt", "text/plain; charset=utf-8"},
    {"md", "text/markdown; charset=utf-8"},
    {"csv", "text/csv; charset=utf-8"},
    {"json", "application/json"},
    {"log", "text/plain; charset=utf-8"},
};

std::string_view lookup(const Entry* table, std::size_t n, std::string_view ext,
                        std::string_view fallback) {
    for (std::size_t i = 0; i < n; ++i) {
        if (table[i].ext == ext) return table[i].type;
    }
    return fallback;
}

bool isKnownImageExt(std::string_view ext) {
    return lookup(IMAGE_TYPES, std::size(IMAGE_TYPES), ext, {}) != std::string_view{};
}

bool isKnownTextExt(std::string_view ext) {
    return lookup(TEXT_TYPES, std::size(TEXT_TYPES), ext, {}) != std::string_view{};
}

/// The media type with any parameters and surrounding space removed, lowercased:
/// "Image/JPEG; charset=x" -> "image/jpeg".
std::string bareType(std::string_view ct) {
    const std::size_t semi = ct.find(';');
    std::string_view head = (semi == std::string_view::npos) ? ct : ct.substr(0, semi);
    while (!head.empty() && (head.front() == ' ' || head.front() == '\t')) head.remove_prefix(1);
    while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) head.remove_suffix(1);
    std::string out;
    out.reserve(head.size());
    for (char c : head) out.push_back(lower(c));
    return out;
}

}  // namespace

std::string extensionOf(std::string_view key) {
    const std::string_view leaf = keys::leaf(key);
    const std::size_t dot = leaf.rfind('.');
    // npos: no dot. 0: a dotfile, whose name is not an extension.
    if (dot == std::string_view::npos || dot == 0) return {};
    const std::string_view ext = leaf.substr(dot + 1);
    if (ext.empty() || ext.size() > MAX_EXTENSION) return {};
    std::string out;
    out.reserve(ext.size());
    for (char c : ext) out.push_back(lower(c));
    return out;
}

std::string_view contentTypeFor(VAR_TYPE t, std::string_view key) {
    switch (t) {
        case VAR_TYPE::IMG:
            return lookup(IMAGE_TYPES, std::size(IMAGE_TYPES), extensionOf(key),
                          OCTET_STREAM);
        case VAR_TYPE::STR:
            return lookup(TEXT_TYPES, std::size(TEXT_TYPES), extensionOf(key),
                          PLAIN_TEXT);
        // Scalars are stored as their raw little-endian bytes, not as text, so
        // octet-stream is the honest answer rather than a lazy default.
        case VAR_TYPE::CHAR:
        case VAR_TYPE::INT:
        case VAR_TYPE::BIG_INT:
        case VAR_TYPE::FLOAT:
        case VAR_TYPE::BIG_FLOAT:
        case VAR_TYPE::BIN:
        case VAR_TYPE::UNSET:
            return OCTET_STREAM;
    }
    return OCTET_STREAM;
}

VAR_TYPE inferVarType(std::string_view contentType, std::string_view key) {
    const std::string ct = bareType(contentType);
    // The declared type leads, because the client knows what it sent.
    if (ct.starts_with("image/")) return VAR_TYPE::IMG;
    if (ct.starts_with("text/") || ct == "application/json") return VAR_TYPE::STR;
    // Then the extension, for a client that sent nothing useful. curl sends
    // application/x-www-form-urlencoded by default, which says nothing.
    const std::string ext = extensionOf(key);
    if (isKnownImageExt(ext)) return VAR_TYPE::IMG;
    if (isKnownTextExt(ext)) return VAR_TYPE::STR;
    return VAR_TYPE::BIN;
}

}  // namespace swiftsearch::http
