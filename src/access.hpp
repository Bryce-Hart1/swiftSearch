#pragma once
/**
 * Who may use the store, and the store's own salt.
 *
 * Two secrets live in the store folder, both written with mode 0600:
 *
 *   store.salt   32 random bytes, made the first time a store is opened. Every
 *                hash the server hands out (ETag, X-SwiftSearch-Hash, the "hash"
 *                field of a PUT) is HMAC-SHA256 of the value keyed by it. Without
 *                the salt, anyone holding a file could hash it and use a hash they
 *                saw (in a log, a proxy cache, a shared link) to learn whether this
 *                store holds that file. With it, a hash means nothing outside this
 *                store, and the same file has unrelated hashes in two stores. It is
 *                still stable for the life of the store, which is all an ETag needs.
 *
 *   access.keys  one line per access key: its id, what it may do, a random salt of
 *                its own, and HMAC-SHA256(that salt, the key). The key itself is
 *                printed once, by `swiftSearch key add`, and written nowhere. A copy
 *                of the folder, a backup or this file therefore holds nothing that
 *                opens the store, and two equal keys would not even look equal.
 *
 * A key is 256 random bits, not a password a person chose, so there is nothing to
 * guess and nothing for a deliberately slow password hash (bcrypt, scrypt) to
 * protect. A fast keyed hash is the right tool, and it keeps the check that every
 * request pays in the microseconds.
 *
 * KEY FORMAT   ssk_<id>_<secret>   77 characters. The id is 8 lowercase hex digits,
 *              not secret, and names the key in the file and in `key list`. The
 *              secret is 64 lowercase hex digits (32 random bytes). A client sends
 *              it as `Authorization: Bearer ssk_…`.
 *
 * KEYS FILE    text, so a key can be read and revoked by hand:
 *                # comment
 *                <id> <read|write|admin> <salt: 32 hex> <hash: 64 hex> [name]
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"
#include "sha256.hpp"

namespace swiftsearch {

inline constexpr const char* SALT_FILE = "store.salt";
inline constexpr const char* KEYS_FILE = "access.keys";

/// Fills `out` from the operating system's random source (getentropy).
/// @throws std::runtime_error if there is none, which is never worth working around.
void fillRandom(std::span<std::uint8_t> out);

/// True when `a` and `b` hold the same bytes. A near miss takes as long as a total
/// miss, so timing the comparison says nothing about how much of a guess was right.
bool equalConstantTime(std::span<const std::uint8_t> a,
                       std::span<const std::uint8_t> b) noexcept;

/**
 * The store's salt, and the file that keeps it.
 *
 * File layout (integers little-endian):
 *   off  size  field
 *     0     4  magic    53 53 53 1A  ('S' 'S' 'S' 0x1A)
 *     4     1  version  1
 *     5    32  salt
 *    37     4  crc32c   over bytes [0, 37)
 *
 * Like store.meta, a file that fails its checks is refused rather than replaced:
 * a new salt would quietly change every hash the store has ever handed out.
 */
class StoreSalt {
public:
    static constexpr std::size_t SIZE = 32;
    static constexpr std::uint32_t MAGIC = 0x1A535353u;  // on disk 53 53 53 1A
    static constexpr std::uint8_t VERSION = 1;
    static constexpr std::size_t FILE_BYTES = 4 + 1 + SIZE + 4;  // 41

    explicit StoreSalt(const std::array<std::uint8_t, SIZE>& bytes) noexcept : _bytes(bytes) {}

    /// Reads <folder>/store.salt, making it first if it is missing. Call it only while
    /// holding the store's lock, so two processes cannot each make a different one.
    /// @throws std::runtime_error if the file is damaged or cannot be written.
    static StoreSalt openOrCreate(const std::filesystem::path& folder);

    /// HMAC-SHA256 of `data`, keyed by this salt.
    Digest hash(std::span<const std::uint8_t> data) const noexcept;

    Bytes encode() const;
    /// nullopt for anything encode() did not write: wrong size, magic, version or CRC.
    static std::optional<StoreSalt> decode(std::span<const std::uint8_t> in);

private:
    std::array<std::uint8_t, SIZE> _bytes;
};

/// What a key may do. Ordered: each level can do everything the one below can.
enum class Access : std::uint8_t {
    None = 0,   // no key, or one that is not in the file
    Read = 1,   // GET and HEAD
    Write = 2,  // also PUT, DELETE and POST
    Admin = 3,  // also /v1/admin/limits, when the server serves it
};

/// "none", "read", "write", "admin".
std::string_view accessName(Access a) noexcept;
/// The inverse, for read, write and admin only.
std::optional<Access> accessFromName(std::string_view name) noexcept;

/// One line of access.keys.
struct KeyEntry {
    static constexpr std::size_t SALT_SIZE = 16;

    std::string id;  // 8 lowercase hex digits, the same as inside the key
    Access access = Access::None;
    std::array<std::uint8_t, SALT_SIZE> salt{};
    Digest hash{};     // HMAC-SHA256(salt, the whole key text)
    std::string name;  // optional label; see isValidKeyName

    /// True when `key` is the key this entry was made from. Constant time in the hash.
    bool matches(std::string_view key) const noexcept;
};

/// The id inside a key, or nullopt when `key` is not shaped like one.
std::optional<std::string_view> keyId(std::string_view key) noexcept;

/// Empty, or up to 64 of A-Z a-z 0-9 . _ -
bool isValidKeyName(std::string_view name) noexcept;

/// The entry for a key you already hold, with a fresh salt. nullopt if `key` is not
/// shaped like a key or `name` is not a valid name.
std::optional<KeyEntry> entryFor(std::string_view key, Access access, std::string name);

struct NewKey {
    std::string key;  // shown to the person once; never stored
    KeyEntry entry;   // what goes in the file
};
/// A fresh random key, with an id that none of `taken` uses.
/// @throws std::invalid_argument for a bad name or Access::None.
NewKey makeKey(Access access, std::string name, std::span<const KeyEntry> taken);

/// access.keys as text, and back. parseKeys reports the first bad line it finds.
std::string formatKeys(std::span<const KeyEntry> keys);
std::expected<std::vector<KeyEntry>, std::string> parseKeys(std::string_view text);

/// A missing file is an empty list, not an error.
std::expected<std::vector<KeyEntry>, std::string> readKeys(const std::filesystem::path& file);
/// Atomically, with mode 0600. @return false if it could not be written.
bool writeKeys(const std::filesystem::path& file, std::span<const KeyEntry> keys);

/**
 * The keys the server checks every request against.
 *
 * The file is looked at again at most once per RECHECK, and reread when it has
 * changed, so `key add` and `key remove` take effect on a running server within a
 * second, with no restart.
 *
 * FAILS CLOSED. A missing file means no keys. A damaged one means no keys too, and
 * says so through `warn`, rather than carrying on with the old list: that list may
 * hold the very key someone just tried to remove.
 *
 * Safe from any thread. The clock is a parameter, as in http/conn.hpp, so a test
 * can step past RECHECK without sleeping.
 */
class AccessKeys {
public:
    using Clock = std::chrono::steady_clock;
    using Warn = std::function<void(const std::string&)>;
    static constexpr std::chrono::seconds RECHECK{1};

    /// Reads `file` now. A problem with it goes to `warn` and leaves no keys.
    AccessKeys(std::filesystem::path file, Warn warn);

    /// What the key in an Authorization header value may do. None when there is no
    /// header, it is not "Bearer <key>", or the key is not in the file.
    Access check(std::optional<std::string_view> authorization, Clock::time_point now);

    /// Keys loaded at the last look at the file.
    std::size_t count() const;
    const std::filesystem::path& file() const noexcept { return _file; }

private:
    /// Enough to tell that the file was replaced: `key add` renames a new file over it.
    struct Stamp {
        bool exists = false;
        std::filesystem::file_time_type mtime{};
        std::uintmax_t size = 0;
        bool operator==(const Stamp&) const = default;
    };
    Stamp stampNow() const noexcept;
    void load(const Stamp& stamp);  // with _mtx held

    std::filesystem::path _file;
    Warn _warn;
    mutable std::mutex _mtx;  // guards everything below
    std::shared_ptr<const std::vector<KeyEntry>> _keys;
    Stamp _stamp;
    Clock::time_point _checked{};
};

}  // namespace swiftsearch
