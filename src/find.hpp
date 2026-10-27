#pragma once
/**
 * LookupTable: for a string key, is its value in memory, on disk, or both, and
 * where on disk is it?
 *
 * Two structures cooperate:
 *
 *   _keys (bstd::ds::ts::Trie)  the set of keys that exist. It is the visibility
 *                               gate: a key is "in the table" exactly when the
 *                               trie says so. A miss is answered here without
 *                               ever touching the map lock, so lookups of absent
 *                               keys never contend with writers.
 *   _map                        key -> Entry. The trie holds words only (no
 *                               payload), so the residency and location live here.
 *
 * Every Entry has at least one of `memory` / `disk` set; an entry with neither
 * would be a key that points nowhere, and erase() exists for that.
 *
 * LOCKING
 *   _write     serializes every operation that changes the key set (put*, erase,
 *              clear) so the trie and the map cannot drift apart.
 *   _map_mtx   guards the map and the entries inside it.
 *   Order is always _write -> _map_mtx; the trie takes its own lock inside.
 *   Publish order is map first then trie, erase order is trie first then map, so
 *   a key the trie reports is always already in the map.
 *
 * VERSIONING WITHOUT VERSION NUMBERS
 *   A disk read or a flush happens outside any lock, so the entry can be
 *   overwritten while it is in flight. The three follow-up calls therefore only
 *   apply if the entry is still what the caller saw:
 *     markFlushed  compares the in-memory buffer pointer (identity = version)
 *     cache        compares the disk location
 *     relocate     compares the old disk location (compaction's swap)
 *   Each returns false when it loses the race, and the caller just drops its work.
 *
 * Values are held as shared_ptr<const Bytes>, so a reader copies a handle, not
 * the bytes, and evict() can never pull a buffer out from under a reader.
 *
 * CRASH BEHAVIOR
 *   This table is NOT durable. It lives entirely in RAM and persists nothing.
 *   After a crash it is empty and must be rebuilt by replaying the segment files
 *   (putDisk per record, last record for a key wins, torn tail truncated). So:
 *
 *   - Whatever only putMemory() has seen is gone: the bytes and the key. After
 *     restart lookup() reports the key as absent, or, if an older version was
 *     already on disk, returns that older version.
 *   - Therefore never acknowledge a write to a client on the strength of
 *     putMemory(). Acknowledged means appended to a segment and fsync'd. The
 *     order is: append, fsync, then putDisk (or markFlushed). Never the reverse;
 *     an entry pointing at bytes that are not on disk is the dangerous failure.
 *     putMemory is only for write-behind staging of values the client has not
 *     been told are safe yet.
 *   - erase() forgets the key in RAM only. Unless a tombstone record was
 *     appended and fsync'd first, replay after a crash brings the key back.
 *   - A crash cannot leave the table half-updated, because the table does not
 *     survive one. Every inconsistency the locking above guards against is a
 *     live-process concern only.
 */

#include "bstd/data_structure/threadsafe/tsTrie.hpp"
#include "bytes.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace swiftsearch {

/// Where a record lives in the segment files. Mirrors the in-memory index entry
/// from the design notes: segment id, byte offset of the record, record length.
struct DiskLocation {
    std::uint32_t segment = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;

    friend bool operator==(const DiskLocation&, const DiskLocation&) = default;
};

enum class Residency : std::uint8_t {
    Memory,  ///< written but not flushed yet; the only copy is in RAM
    Disk,    ///< on disk only; reading it costs a seek
    Both,    ///< on disk, with a cached copy in RAM
};

/// What lookup() hands back. A snapshot: it stays valid, but the table may have
/// moved on by the time the caller looks at it.
struct Entry {
    std::optional<DiskLocation> disk;
    std::shared_ptr<const Bytes> memory;

    bool inMemory() const noexcept { return memory != nullptr; }
    bool onDisk() const noexcept { return disk.has_value(); }

    Residency residency() const noexcept {
        if(inMemory() && onDisk()){
        return Residency::Both; 
        }
        return inMemory() ? Residency::Memory : Residency::Disk;
    }
};

class LookupTable {
    struct KeyHash {
        using is_transparent = void;  // lets find() take a string_view without building a std::string
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    bstd::ds::ts::Trie _keys;
    std::unordered_map<std::string, Entry, KeyHash, std::equal_to<>> _map;
    mutable std::shared_mutex _map_mtx;
    std::mutex _write;

    /// Caller holds _write. Insert or replace, then make the key visible.
    void publish(const std::string& key, Entry entry){
        {
            std::unique_lock lock(_map_mtx);
            _map.insert_or_assign(key, std::move(entry));
        }
        // the trie counts duplicates, so only add a key it has not seen
        if(!_keys.find(key)){
            _keys.add(key);
        }
    }

    /// Run fn on the entry for key under the map's exclusive lock.
    /// @return false if the key is absent, otherwise whatever fn returned.
    template<typename Fn>
    bool withEntry(std::string_view key, Fn&& fn){
        std::unique_lock lock(_map_mtx);
        const auto itr = _map.find(key);
        if(itr == _map.end()) return false;
        return fn(itr->second);
    }

public:
    LookupTable() = default;

    // the trie and both mutexes are immovable
    LookupTable(const LookupTable&) = delete;
    LookupTable& operator=(const LookupTable&) = delete;
    LookupTable(LookupTable&&) = delete;
    LookupTable& operator=(LookupTable&&) = delete;

    /** @attention reads ------------------------------------------------------ */

    /// @return the entry for key, or nullopt if the table has never heard of it.
    std::optional<Entry> lookup(std::string_view key) const {
        const std::string k(key);
        if(!_keys.find(k)) return std::nullopt;  // fast negative, no map lock

        std::shared_lock lock(_map_mtx);
        const auto itr = _map.find(key);
        if(itr == _map.end()) return std::nullopt;  // erased between the two checks
        return itr->second;
    }

    /// Cheapest existence check: trie only.
    bool contains(std::string_view key) const {
        return _keys.find(std::string(key));
    }

    /// Number of distinct keys.
    std::size_t size() const {
        return _keys.getWordCount();
    }

    /// Snapshot of every key. Order is the trie's depth-first order.
    std::vector<std::string> keys() const {
        return _keys.getWords();
    }

    /** @attention writes ----------------------------------------------------- */

    /// New or overwritten value that exists only in RAM so far. Any old disk
    /// location is dropped: it holds the previous version.
    /// NOT crash safe: a crash loses this value and key (see CRASH BEHAVIOR).
    /// Do not acknowledge a client write until it has been flushed to disk.
    /// @return false for an empty key (the trie cannot represent one).
    bool putMemory(std::string_view key, Bytes bytes){
        if(key.empty()) return false;
        auto data = std::make_shared<const Bytes>(std::move(bytes));

        std::lock_guard w(_write);
        publish(std::string(key), Entry{std::nullopt, std::move(data)});
        return true;
    }

    /// Key whose value is already on disk and not cached, e.g. while rebuilding
    /// the index from the segments at startup.
    /// @return false for an empty key.
    bool putDisk(std::string_view key, const DiskLocation& where){
        if(key.empty()) return false;

        std::lock_guard w(_write);
        publish(std::string(key), Entry{where, nullptr});
        return true;
    }

    /// The flusher wrote `written` (the buffer it got from lookup()) to `where`.
    /// Attaches the location only if the entry still holds that exact buffer; if
    /// the key was overwritten meanwhile, `where` is a stale version.
    /// @return false if the key is gone or was overwritten.
    bool markFlushed(std::string_view key,
                     const std::shared_ptr<const Bytes>& written,
                     const DiskLocation& where){
        return withEntry(key, [&](Entry& e){
            if(e.memory != written) return false;
            e.disk = where;
            return true;
        });
    }

    /// A disk read of `from` finished; keep the bytes in RAM. Applies only if the
    /// entry still points at `from` and has no cached copy already.
    /// @return false if the entry changed under the reader.
    bool cache(std::string_view key,
               const DiskLocation& from,
               std::shared_ptr<const Bytes> data){
        return withEntry(key, [&](Entry& e){
            if(e.disk != from || e.memory) return false;
            e.memory = std::move(data);
            return true;
        });
    }

    /// Drop the RAM copy, keeping the key. Refuses when RAM holds the only copy.
    /// Readers that already copied the handle keep their bytes alive.
    /// @return true if a cached copy was dropped.
    bool evict(std::string_view key){
        return withEntry(key, [](Entry& e){
            if(!e.memory || !e.disk) return false;
            e.memory.reset();
            return true;
        });
    }

    /// Compaction moved a record. Swap in `to` only if the entry still points at
    /// `from`; otherwise a newer write won and the compactor skips this record.
    /// @return false if the entry no longer points at `from`.
    bool relocate(std::string_view key,
                  const DiskLocation& from,
                  const DiskLocation& to){
        return withEntry(key, [&](Entry& e){
            if(e.disk != from) return false;
            e.disk = to;
            return true;
        });
    }

    /// Forget a key. Does not touch the disk; the old record becomes dead bytes
    /// that compaction reclaims.
    /// @return true if the key existed.
    bool erase(std::string_view key){
        const std::string k(key);

        std::lock_guard w(_write);
        _keys.remove(k, true);  // hide it first, then free the entry
        std::unique_lock lock(_map_mtx);
        return _map.erase(k) > 0;
    }

    void clear(){
        std::lock_guard w(_write);
        _keys.clear();
        std::unique_lock lock(_map_mtx);
        _map.clear();
    }
};

}  // namespace swiftsearch