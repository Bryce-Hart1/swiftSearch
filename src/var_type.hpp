#pragma once
/**
 * What kind of value a record's payload holds.
 *
 * This is one byte of the record header, at offset 7. It answers the question a
 * reader cannot answer from the bytes themselves: a client asking for
 * `photos/2026/beach.jpg` has to be told it is receiving an image, not a
 * number, and nothing in a blob of JPEG bytes says so.
 *
 * WHY THIS LIVES IN ITS OWN HEADER, away from constants.hpp where it started:
 * constants.hpp constructs a `bstd::store::Logger` at namespace scope, and that
 * constructor opens a file and throws if it cannot -- before main(), in every
 * binary that includes the header. The format layer and the test binary are
 * deliberately forbidden from including it for that reason (see
 * docs/revival-checklist.md), and record.hpp needs this enum. constants.hpp
 * includes this file, so every existing VAR_TYPE user is unaffected.
 *
 * THE VALUES LAND ON DISK. Never renumber one, only append. This is the same
 * rule bstd::store::serialize's `rowType` states about its own tags, for the
 * same reason: a renumbered tag silently reinterprets every record ever
 * written. The explicit `: std::uint8_t` is part of that -- without it the
 * underlying width is the compiler's choice, which is not something a file
 * format may depend on.
 *
 * UNSET is not padding. A tombstone has no payload and a box's payload is
 * BoxMeta, so in both cases the flag bit already determines the shape and a
 * type byte would be a second answer to a settled question. They write UNSET.
 * Putting it at 0 also means a zeroed header decodes as "nobody said" rather
 * than as CHAR, which is the more honest reading of an all-zero byte.
 *
 * The names are also the wire spelling, used by the `X-SwiftSearch-Type`
 * header. var_type_name and var_type_from_name are kept next to each other on
 * purpose: two spellings of the same enum maintained in separate files is the
 * kind of drift this codebase designs out rather than tests for.
 */

#include <cstdint>
#include <optional>
#include <string_view>

enum class VAR_TYPE : std::uint8_t {
    UNSET     = 0,  // tombstones and boxes; "the caller did not say"
    CHAR      = 1,  // 1 byte
    INT       = 2,  // 4 bytes
    BIG_INT   = 3,  // 8 bytes
    FLOAT     = 4,  // 4 bytes
    BIG_FLOAT = 5,  // 8 bytes
    IMG       = 6,  // varies
    BIN       = 7,  // varies
    STR       = 8,  // varies
    // Appended, so it sits at the end of the list rather than next to IMG
    // where it would read better. Text order matches numeric order here on
    // purpose: for a format header that is one less thing to get wrong when
    // reading a hex dump, and the values are what land on disk.
    VIDEO     = 9,  // varies
};

/// The largest value this build knows. Appending a type means bumping this.
inline constexpr std::uint8_t VAR_TYPE_MAX = static_cast<std::uint8_t>(VAR_TYPE::VIDEO);

/// True when `v` names a type this build understands. The record decoder uses
/// this the same way it uses knownCompression: an intact header naming an
/// unknown type is a policy refusal, not corruption.
inline constexpr bool is_known_var_type(std::uint8_t v) noexcept {
    return v <= VAR_TYPE_MAX;
}

/// Lowercase, stable, and the spelling used on the wire. Returns a view into
/// static storage: no allocation.
inline constexpr std::string_view var_type_name(VAR_TYPE t) noexcept {
    switch (t) {
        case VAR_TYPE::UNSET:     return "unset";
        case VAR_TYPE::CHAR:      return "char";
        case VAR_TYPE::INT:       return "int";
        case VAR_TYPE::BIG_INT:   return "big_int";
        case VAR_TYPE::BIG_FLOAT: return "big_float";
        case VAR_TYPE::FLOAT:     return "float";
        case VAR_TYPE::IMG:       return "img";
        case VAR_TYPE::BIN:       return "bin";
        case VAR_TYPE::STR:       return "str";
        case VAR_TYPE::VIDEO:     return "video";
    }
    return "unset";
}

/// The inverse of var_type_name. Case-insensitive over ASCII, because it parses
/// a request header. nullopt for anything unrecognised -- the caller reports a
/// 400 rather than quietly falling back to a type the client did not ask for.
inline constexpr std::optional<VAR_TYPE> var_type_from_name(std::string_view name) noexcept {
    constexpr auto fold = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char c = a[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            if (c != b[i]) return false;
        }
        return true;
    };
    for (std::uint8_t v = 0; v <= VAR_TYPE_MAX; ++v) {
        const auto t = static_cast<VAR_TYPE>(v);
        if (fold(name, var_type_name(t))) return t;
    }
    return std::nullopt;
}
