#include "box_meta.hpp"

#include <stdexcept>

namespace swiftsearch {
namespace {

constexpr std::size_t O_VERSION = 0;
constexpr std::size_t O_FLAGS = 1;
constexpr std::size_t O_RESERVED = 2;
constexpr std::size_t O_CREATED = 4;
constexpr std::size_t O_NAME_LEN = 12;
constexpr std::size_t O_TAG_COUNT = 14;
static_assert(O_TAG_COUNT + 2 == BOX_META_FIXED_SIZE);

}  // namespace

std::string_view describe(BoxMetaStatus s) noexcept {
    switch (s) {
        case BoxMetaStatus::Ok: return "ok";
        case BoxMetaStatus::Truncated: return "truncated box metadata";
        case BoxMetaStatus::BadVersion: return "unsupported box metadata version";
        case BoxMetaStatus::BadReserved: return "box metadata reserved field not zero";
        case BoxMetaStatus::CountOverflow: return "box metadata count past its cap";
    }
    return "unknown";
}

Bytes BoxMeta::encode() const {
    // Throws for the same reason Envelope::encodeTo does: this is a struct the
    // caller just built, so an oversized field is a bug in the caller, not
    // untrusted input. decode() never throws.
    if (display_name.size() > MAX_BOX_NAME_LEN) {
        throw std::length_error("BoxMeta::encode: display_name exceeds MAX_BOX_NAME_LEN");
    }
    if (tags.size() > MAX_BOX_TAGS) {
        throw std::length_error("BoxMeta::encode: too many tags");
    }

    Bytes out;
    out.insert(out.end(), BOX_META_FIXED_SIZE, 0);
    std::uint8_t* p = out.data();
    p[O_VERSION] = BOX_META_VERSION;
    p[O_FLAGS] = 0;
    store_u16_le(p + O_RESERVED, 0);
    store_i64_le(p + O_CREATED, created_ns);
    store_u16_le(p + O_NAME_LEN, static_cast<std::uint16_t>(display_name.size()));
    store_u16_le(p + O_TAG_COUNT, static_cast<std::uint16_t>(tags.size()));

    const auto* name = reinterpret_cast<const std::uint8_t*>(display_name.data());
    out.insert(out.end(), name, name + display_name.size());

    for (const std::string& tag : tags) {
        if (tag.size() > MAX_BOX_NAME_LEN) {
            throw std::length_error("BoxMeta::encode: tag too long");
        }
        put_u16_le(out, static_cast<std::uint16_t>(tag.size()));
        const auto* t = reinterpret_cast<const std::uint8_t*>(tag.data());
        out.insert(out.end(), t, t + tag.size());
    }
    return out;
}

std::expected<BoxMeta, BoxMetaStatus> BoxMeta::decode(
    std::span<const std::uint8_t> in) noexcept {
    if (in.size() < BOX_META_FIXED_SIZE) {
        return std::unexpected(BoxMetaStatus::Truncated);
    }
    const std::uint8_t* p = in.data();

    if (p[O_VERSION] != BOX_META_VERSION) {
        return std::unexpected(BoxMetaStatus::BadVersion);
    }
    if (p[O_FLAGS] != 0 || load_u16_le(p + O_RESERVED) != 0) {
        return std::unexpected(BoxMetaStatus::BadReserved);
    }

    const std::uint16_t nameLen = load_u16_le(p + O_NAME_LEN);
    const std::uint16_t tagCount = load_u16_le(p + O_TAG_COUNT);

    // Caps before any reserve, so a corrupt count cannot allocate.
    if (nameLen > MAX_BOX_NAME_LEN || tagCount > MAX_BOX_TAGS) {
        return std::unexpected(BoxMetaStatus::CountOverflow);
    }
    // Each remaining tag needs at least its 2-byte length prefix. Checked up
    // front so a count of 1024 over a 3-byte buffer is rejected before the loop.
    if (!fits(in, BOX_META_FIXED_SIZE, static_cast<std::size_t>(nameLen) + tagCount * 2u)) {
        return std::unexpected(BoxMetaStatus::Truncated);
    }

    BoxMeta meta;
    meta.created_ns = load_i64_le(p + O_CREATED);

    std::size_t cursor = BOX_META_FIXED_SIZE;
    meta.display_name.assign(reinterpret_cast<const char*>(p + cursor), nameLen);
    cursor += nameLen;

    meta.tags.reserve(tagCount);
    for (std::uint16_t i = 0; i < tagCount; ++i) {
        const auto len = get_u16_le(in, cursor);
        if (!len) return std::unexpected(BoxMetaStatus::Truncated);
        cursor += 2;
        if (*len > MAX_BOX_NAME_LEN) return std::unexpected(BoxMetaStatus::CountOverflow);
        if (!fits(in, cursor, *len)) return std::unexpected(BoxMetaStatus::Truncated);
        meta.tags.emplace_back(reinterpret_cast<const char*>(p + cursor), *len);
        cursor += *len;
    }
    return meta;
}

}  // namespace swiftsearch
