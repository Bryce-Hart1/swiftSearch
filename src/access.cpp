#include "access.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <sys/random.h>
#include <unistd.h>

#include "crc32c.hpp"
#include "file_sync.hpp"

namespace swiftsearch {
namespace {

constexpr std::string_view KEY_PREFIX = "ssk_";
constexpr std::size_t ID_HEX = 8;
constexpr std::size_t SECRET_HEX = 64;
constexpr std::size_t KEY_LEN = KEY_PREFIX.size() + ID_HEX + 1 + SECRET_HEX;  // 77
constexpr std::size_t MAX_NAME_LEN = 64;

std::span<const std::uint8_t> bytesOf(std::string_view s) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

bool isLowerHex(std::string_view s) noexcept {
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string hexOf(std::span<const std::uint8_t> in) {
    static constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    out.reserve(in.size() * 2);
    for (std::uint8_t b : in) {
        out.push_back(DIGITS[b >> 4]);
        out.push_back(DIGITS[b & 0x0F]);
    }
    return out;
}

/// Lowercase only, exactly out.size() * 2 digits.
bool fromHex(std::string_view in, std::span<std::uint8_t> out) noexcept {
    if (in.size() != out.size() * 2 || !isLowerHex(in)) return false;
    const auto nibble = [](char c) {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(nibble(in[2 * i]) << 4 | nibble(in[2 * i + 1]));
    }
    return true;
}

bool isBlank(char c) noexcept { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
    while (!s.empty() && (isBlank(s.back()) || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

Digest keyHash(std::span<const std::uint8_t> salt, std::string_view key) noexcept {
    return hmacSha256(salt, bytesOf(key));
}

}  // namespace


/* RANDOMNESS AND COMPARISON ---------------------------------------------------------- */

void fillRandom(std::span<std::uint8_t> out) {
    // getentropy hands out at most 256 bytes per call, on macOS and Linux alike.
    constexpr std::size_t MAX_PER_CALL = 256;
    while (!out.empty()) {
        const std::size_t n = std::min(out.size(), MAX_PER_CALL);
        if (::getentropy(out.data(), n) != 0) {
            throw std::system_error(errno, std::generic_category(), "getentropy");
        }
        out = out.subspan(n);
    }
}

bool equalConstantTime(std::span<const std::uint8_t> a,
                       std::span<const std::uint8_t> b) noexcept {
    if (a.size() != b.size()) return false;
    // volatile so the compiler cannot turn the loop back into an early exit.
    volatile std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}


/* THE STORE'S SALT ------------------------------------------------------------------- */

StoreSalt StoreSalt::openOrCreate(const std::filesystem::path& folder) {
    const std::filesystem::path file = folder / SALT_FILE;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        const Bytes buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto salt = decode(buf);
        if (in.bad()) salt.reset();
        if (!salt) throw std::runtime_error(file.string() + " is damaged");
        return *salt;
    }
    if (ec) throw std::system_error(ec, "look for " + file.string());

    std::array<std::uint8_t, SIZE> bytes{};
    fillRandom(bytes);
    const StoreSalt fresh(bytes);
    if (!writeFileAtomically(file, fresh.encode(), 0600)) {
        throw std::runtime_error("could not write " + file.string());
    }
    return fresh;
}

Digest StoreSalt::hash(std::span<const std::uint8_t> data) const noexcept {
    return hmacSha256(_bytes, data);
}

Bytes StoreSalt::encode() const {
    Bytes out;
    out.reserve(FILE_BYTES);
    put_u32_le(out, MAGIC);
    put_u8(out, VERSION);
    out.insert(out.end(), _bytes.begin(), _bytes.end());
    put_u32_le(out, crc32c(out));
    return out;
}

std::optional<StoreSalt> StoreSalt::decode(std::span<const std::uint8_t> in) {
    if (in.size() != FILE_BYTES) return std::nullopt;
    if (load_u32_le(in.data()) != MAGIC || in[4] != VERSION) return std::nullopt;
    const std::size_t crcAt = FILE_BYTES - 4;
    if (load_u32_le(in.data() + crcAt) != crc32c(in.first(crcAt))) return std::nullopt;
    std::array<std::uint8_t, SIZE> bytes{};
    std::copy(in.begin() + 5, in.begin() + 5 + SIZE, bytes.begin());
    return StoreSalt(bytes);
}


/* KEYS ------------------------------------------------------------------------------- */

std::string_view accessName(Access a) noexcept {
    switch (a) {
        case Access::None: return "none";
        case Access::Read: return "read";
        case Access::Write: return "write";
        case Access::Admin: return "admin";
    }
    return "none";
}

std::optional<Access> accessFromName(std::string_view name) noexcept {
    if (name == "read") return Access::Read;
    if (name == "write") return Access::Write;
    if (name == "admin") return Access::Admin;
    return std::nullopt;
}

bool KeyEntry::matches(std::string_view key) const noexcept {
    const Digest got = keyHash(salt, key);
    return equalConstantTime(got, hash);
}

std::optional<std::string_view> keyId(std::string_view key) noexcept {
    if (key.size() != KEY_LEN || !key.starts_with(KEY_PREFIX)) return std::nullopt;
    const std::string_view id = key.substr(KEY_PREFIX.size(), ID_HEX);
    const std::string_view secret = key.substr(KEY_PREFIX.size() + ID_HEX + 1);
    if (key[KEY_PREFIX.size() + ID_HEX] != '_' || !isLowerHex(id) || !isLowerHex(secret)) {
        return std::nullopt;
    }
    return id;
}

bool isValidKeyName(std::string_view name) noexcept {
    if (name.size() > MAX_NAME_LEN) return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::optional<KeyEntry> entryFor(std::string_view key, Access access, std::string name) {
    const auto id = keyId(key);
    if (!id || !isValidKeyName(name)) return std::nullopt;
    KeyEntry e;
    e.id.assign(*id);
    e.access = access;
    fillRandom(e.salt);
    e.hash = keyHash(e.salt, key);
    e.name = std::move(name);
    return e;
}

NewKey makeKey(Access access, std::string name, std::span<const KeyEntry> taken) {
    if (access == Access::None) throw std::invalid_argument("a key must be read, write or admin");
    if (!isValidKeyName(name)) {
        throw std::invalid_argument("a key name is at most 64 of A-Z a-z 0-9 . _ -");
    }
    for (;;) {
        std::array<std::uint8_t, ID_HEX / 2> idBytes{};
        std::array<std::uint8_t, SECRET_HEX / 2> secret{};
        fillRandom(idBytes);
        fillRandom(secret);
        const std::string id = hexOf(idBytes);
        bool clash = false;
        for (const KeyEntry& k : taken) clash = clash || k.id == id;
        if (clash) continue;  // 1 in 4 billion per existing key; just roll again

        std::string key(KEY_PREFIX);
        key.append(id).append("_").append(hexOf(secret));
        auto entry = entryFor(key, access, std::move(name));
        return NewKey{std::move(key), std::move(*entry)};  // entryFor accepts every key made here
    }
}


/* THE KEYS FILE ---------------------------------------------------------------------- */

std::string formatKeys(std::span<const KeyEntry> keys) {
    std::string out =
        "# swiftSearch access keys: id, access, salt, HMAC-SHA256(salt, key), name.\n"
        "# Only salted hashes are kept here; a key is shown once, when it is made.\n"
        "# Manage with `swiftSearch key add|list|remove`. Deleting a line revokes that key.\n";
    for (const KeyEntry& k : keys) {
        out.append(k.id).append(" ").append(accessName(k.access)).append(" ")
            .append(hexOf(k.salt)).append(" ").append(to_hex(k.hash));
        if (!k.name.empty()) out.append(" ").append(k.name);
        out.append("\n");
    }
    return out;
}

std::expected<std::vector<KeyEntry>, std::string> parseKeys(std::string_view text) {
    std::vector<KeyEntry> out;
    std::size_t lineNo = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNo;
        if (line.empty() || line.front() == '#') continue;

        std::vector<std::string_view> fields;
        while (!line.empty()) {
            std::size_t end = 0;
            while (end < line.size() && !isBlank(line[end])) ++end;
            fields.push_back(line.substr(0, end));
            line = trim(line.substr(end));
        }
        const auto bad = [lineNo](std::string_view what) {
            return std::unexpected("line " + std::to_string(lineNo) + ": " + std::string(what));
        };
        if (fields.size() != 4 && fields.size() != 5) {
            return bad("expected id, access, salt, hash and an optional name");
        }

        KeyEntry k;
        if (fields[0].size() != ID_HEX || !isLowerHex(fields[0])) return bad("bad id");
        k.id.assign(fields[0]);
        const auto access = accessFromName(fields[1]);
        if (!access) return bad("access must be read, write or admin");
        k.access = *access;
        if (!fromHex(fields[2], k.salt)) return bad("bad salt");
        if (!fromHex(fields[3], k.hash)) return bad("bad hash");
        if (fields.size() == 5) {
            if (!isValidKeyName(fields[4])) return bad("bad name");
            k.name.assign(fields[4]);
        }
        for (const KeyEntry& other : out) {
            if (other.id == k.id) return bad("id " + k.id + " is listed twice");
        }
        out.push_back(std::move(k));
    }
    return out;
}

std::expected<std::vector<KeyEntry>, std::string> readKeys(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        if (ec) return std::unexpected("could not look for it: " + ec.message());
        return std::vector<KeyEntry>{};
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::string("could not be opened"));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::unexpected(std::string("could not be read"));
    return parseKeys(text);
}

bool writeKeys(const std::filesystem::path& file, std::span<const KeyEntry> keys) {
    const std::string text = formatKeys(keys);
    return writeFileAtomically(file, bytesOf(text), 0600);
}


/* THE SERVER'S VIEW ------------------------------------------------------------------ */

AccessKeys::AccessKeys(std::filesystem::path file, Warn warn)
    : _file(std::move(file)), _warn(std::move(warn)) {
    std::lock_guard<std::mutex> lock(_mtx);
    load(stampNow());
}

AccessKeys::Stamp AccessKeys::stampNow() const noexcept {
    Stamp s;
    std::error_code ec;
    s.exists = std::filesystem::exists(_file, ec) && !ec;
    if (!s.exists) return s;
    s.mtime = std::filesystem::last_write_time(_file, ec);
    s.size = std::filesystem::file_size(_file, ec);
    return s;
}

void AccessKeys::load(const Stamp& stamp) {
    _stamp = stamp;
    auto parsed = readKeys(_file);
    if (!parsed) {
        _keys = std::make_shared<const std::vector<KeyEntry>>();
        if (_warn) {
            _warn(_file.string() + ": " + parsed.error() +
                  "; every request needing a key is refused until it is fixed");
        }
        return;
    }
    _keys = std::make_shared<const std::vector<KeyEntry>>(std::move(*parsed));
}

Access AccessKeys::check(std::optional<std::string_view> authorization, Clock::time_point now) {
    std::shared_ptr<const std::vector<KeyEntry>> keys;
    {
        std::lock_guard<std::mutex> lock(_mtx);
        // `now < _checked` covers a test clock that went backwards: look again then too.
        if (now >= _checked + RECHECK || now < _checked) {
            _checked = now;
            const Stamp s = stampNow();
            if (s != _stamp) load(s);
        }
        keys = _keys;
    }

    if (!authorization) return Access::None;
    // "Bearer" is case-insensitive (RFC 9110 section 11.1); the key is not.
    std::string_view v = trim(*authorization);
    constexpr std::string_view SCHEME = "bearer";
    if (v.size() <= SCHEME.size() || !equalsIgnoringCase(v.substr(0, SCHEME.size()), SCHEME) ||
        !isBlank(v[SCHEME.size()])) {
        return Access::None;
    }
    const std::string_view key = trim(v.substr(SCHEME.size()));
    const auto id = keyId(key);
    if (!id) return Access::None;
    for (const KeyEntry& k : *keys) {
        if (k.id == *id) return k.matches(key) ? k.access : Access::None;
    }
    return Access::None;
}

std::size_t AccessKeys::count() const {
    std::lock_guard<std::mutex> lock(_mtx);
    return _keys ? _keys->size() : 0;
}

}  // namespace swiftsearch
