#pragma once
/**
 * A JSON writer, and only a writer.
 *
 * There is no parser here because nothing needs one: box metadata arrives in
 * request headers, and a blob's payload is raw bytes. A hand-rolled parser is a
 * few hundred lines of attack surface that would exist for no caller, so it is
 * not written until something actually sends a JSON body.
 *
 * It appends to a caller-owned std::string rather than owning a buffer, so a
 * response body can be built in one allocation alongside everything else.
 *
 * STRUCTURE IS THE CALLER'S RESPONSIBILITY. The writer tracks commas and
 * nesting depth, which is the part that is tedious and easy to get wrong by
 * hand; it does not verify that every obj() has a matching endObj(). An
 * unbalanced document is a bug in the caller, caught by the test that compares
 * output to an expected literal, in the same spirit as Envelope::encodeTo
 * throwing on an oversized key: malformed input the caller built itself is a
 * programmer error, not untrusted data.
 *
 * INPUT MUST BE WELL-FORMED UTF-8, and the dock guarantees it: keys are
 * validated by http::isValidUtf8 on the way in. This matters because JSON has
 * no escape for a raw byte -- \uXXXX encodes code points -- so a string holding
 * 0xFF simply cannot be written validly. Rather than silently emit a broken
 * document, escapeTo passes bytes >= 0x80 through untouched and relies on that
 * guarantee. See the note in url.hpp.
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace swiftsearch::http {

/// Appends `s` as a quoted JSON string, including the surrounding quotes.
/// Escapes the two characters that must be escaped (" and \), uses the short
/// forms for the five control characters that have them, and \u00XX for every
/// other byte below 0x20. 0x7F is left raw: DEL is a legal JSON string
/// character, and escaping it would be a guess about the consumer.
void escapeTo(std::string& out, std::string_view s);

/// Comma and depth bookkeeping over a std::string. Every method returns *this
/// so a document reads as one expression.
class Json {
public:
    explicit Json(std::string& out) noexcept : _out(out) {}

    Json& obj();
    Json& endObj();
    Json& arr();
    Json& endArr();

    /// An object member name. The next value written belongs to it, so no
    /// comma is inserted between them.
    Json& key(std::string_view name);

    Json& val(std::string_view s);
    Json& val(std::uint64_t v);
    Json& val(std::int64_t v);
    Json& val(bool b);

    /// Appends an already-valid JSON fragment, for composing a document from a
    /// part built elsewhere. Participates in comma tracking; its contents are
    /// not checked.
    Json& raw(std::string_view fragment);

    /// A name and value in one call, which is what almost every line is.
    Json& field(std::string_view name, std::string_view s) { return key(name).val(s); }
    Json& field(std::string_view name, std::uint64_t v) { return key(name).val(v); }
    Json& field(std::string_view name, std::int64_t v) { return key(name).val(v); }
    Json& field(std::string_view name, bool b) { return key(name).val(b); }

private:
    /// Writes a separating comma if this container already holds an element,
    /// then records that it does. A value immediately after a key is that key's
    /// value, never a sibling, so it is exempt.
    void sep();

    std::string& _out;
    /// Bit d-1 is set when the container at depth d already holds an element.
    /// A bitmask rather than a stack because 32 levels is far more than any
    /// response here nests, and it needs no allocation.
    std::uint32_t _nonEmpty = 0;
    std::uint32_t _depth = 0;
    bool _afterKey = false;
};

}  // namespace swiftsearch::http
