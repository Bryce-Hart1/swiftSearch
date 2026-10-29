#include "bstd/store/serialize.hpp"
#include "bytes.hpp"
#include "compact.hpp"
#include "crc32c.hpp"
#include "find.hpp"
#include "file_sync.hpp"
#include "record.hpp"
#include "start_date.hpp"
#include "threading.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>




/**
 * SwiftSearch: an append-only, crash-safe key/value store.
 *
 * Every Put/Delete appends one record (record.hpp) to the newest segment file
 * and fsyncs it BEFORE the call returns, then updates the in-memory LookupTable
 * (find.hpp). The table is never persisted: on startup the segments are replayed
 * in (segment id, offset) order and the last record for a key wins.
 *
 * Writers are serialized by _write_mtx, so the order records land on disk is the
 * order the table sees them, which is what makes replay agree with the live table.
 * Readers never take it.
 *
 * Overwritten and deleted values stay in their segment as dead bytes, counted per
 * segment in _dead, until compact() copies the live records forward and deletes
 * the old file. See compact() for why that is crash safe.
 */
class SwiftSearch{
/** @attention all usings                        */
    using fsPath = std::filesystem::path;
    using Error = std::optional<std::string>;
    static constexpr std::nullopt_t GOOD = std::nullopt;
    static constexpr std::string BAD(const std::string& mes){
        return mes;
    }

    static constexpr std::uint64_t DEFAULT_SEGMENT_LIMIT = 256ull * 1024ull * 1024ull;
    static constexpr std::size_t DEFAULT_CACHE_BUDGET = 64ull * 1024ull * 1024ull;
    static constexpr const char* META_FILE = "store.meta"; //holds the StartDate
    //compaction copies records forward this many bytes at a time: one append and one fsync per
    //batch, and the most a client write waits on it
    static constexpr std::uint64_t COMPACT_BATCH_BYTES = 4ull * 1024ull * 1024ull;
    //a read that fails because compaction moved the record looks again, at most this many times
    static constexpr int READ_ATTEMPTS = 3;


    public:
    /// A value and the type it was stored as, which is what GetTyped hands back.
    struct Value{
        swiftsearch::Bytes bytes;
        VAR_TYPE type = VAR_TYPE::BIN;
    };


    private:

    /**
     * One segment file. Reference counted by the SwiftSearch that owns it, so a
     * reader that copied the pointer keeps the descriptor open for as long as it
     * is reading, whatever happens to the segment table meanwhile.
     *
     * Reads use pread, which needs no lock and may run beside an append: a reader
     * only asks for offsets the table already published, and the table is only
     * updated after the append (and its fsync) finished.
     */
    class LockableFile{

        private:
        int _fd = -1;
        std::uint32_t _id;
        fsPath _path;
        mutable std::mutex _mtx; //guards _size and serializes appenders
        std::uint64_t _size = 0; //next write position == bytes that are valid

        bool flush() const{
            return swiftsearch::syncDescriptor(_fd);
        }

        public:
        LockableFile(const fsPath& path, const std::uint32_t id) : _id(id), _path(path){
            _fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
            if(_fd < 0){
                throw std::system_error(errno, std::generic_category(), "open " + path.string());
            }
            struct stat st{};
            if(::fstat(_fd, &st) != 0){
                const int e = errno;
                ::close(_fd);
                throw std::system_error(e, std::generic_category(), "stat " + path.string());
            }
            _size = static_cast<std::uint64_t>(st.st_size);
        }

        ~LockableFile(){
            if(_fd >= 0){
                ::close(_fd);
            }
        }

        LockableFile(const LockableFile&) = delete;
        LockableFile& operator=(const LockableFile&) = delete;

        std::uint32_t id() const noexcept{
            return _id;
        }

        const fsPath& path() const noexcept{
            return _path;
        }

        std::uint64_t size() const{
            std::lock_guard<std::mutex> lock(_mtx);
            return _size;
        }

        /**
         * Writes bytes at the end and fsyncs. @return the offset they landed at.
         * On failure the file is cut back to where it was, so a half written
         * record is not left behind for the next append to trip over.
         */
        std::expected<std::uint64_t, std::string> append(std::span<const std::uint8_t> bytes){
            std::lock_guard<std::mutex> lock(_mtx);
            const std::uint64_t at = _size;

            std::size_t done = 0;
            while(done < bytes.size()){
                const ssize_t n = ::pwrite(_fd, bytes.data() + done, bytes.size() - done,
                                           static_cast<off_t>(at + done));
                if(n < 0){
                    if(errno == EINTR){
                        continue;
                    }
                    const std::string why = std::generic_category().message(errno);
                    (void)::ftruncate(_fd, static_cast<off_t>(at));
                    return std::unexpected("write failed: " + why);
                }
                done += static_cast<std::size_t>(n);
            }
            if(!flush()){
                const std::string why = std::generic_category().message(errno);
                (void)::ftruncate(_fd, static_cast<off_t>(at));
                return std::unexpected("fsync failed: " + why);
            }
            _size = at + bytes.size();
            return at;
        }

        /// @return exactly `length` bytes from `offset`, or nullopt if the file is shorter or the read fails
        std::optional<swiftsearch::Bytes> readAt(const std::uint64_t offset, const std::uint64_t length) const{
            constexpr std::uint64_t MAX_RECORD = swiftsearch::RECORD_OVERHEAD
                + swiftsearch::MAX_KEY_LEN + swiftsearch::MAX_PAYLOAD_LEN;
            if(length > MAX_RECORD){
                return std::nullopt; //a corrupt length must not become a huge allocation
            }
            swiftsearch::Bytes out(static_cast<std::size_t>(length));
            std::size_t done = 0;
            while(done < out.size()){
                const ssize_t n = ::pread(_fd, out.data() + done, out.size() - done,
                                          static_cast<off_t>(offset + done));
                if(n < 0 && errno == EINTR){
                    continue;
                }
                if(n <= 0){
                    return std::nullopt;
                }
                done += static_cast<std::size_t>(n);
            }
            return out;
        }

        /// Cut the file to newSize (drops a torn tail). @return false if it could not be done
        bool truncate(const std::uint64_t newSize){
            std::lock_guard<std::mutex> lock(_mtx);
            if(::ftruncate(_fd, static_cast<off_t>(newSize)) != 0){
                return false;
            }
            _size = newSize;
            return flush();
        }
    };


    //opens the start date in `folder`, creating the file on the very first run. `created` says which happened.
    //A file that exists but is damaged is an error, not a reason to start over: that would shift every timestamp.
    static swiftsearch::StartDate openStartDate(const fsPath& folder, bool& created){
        const fsPath file = folder / META_FILE;
        if(std::filesystem::exists(file)){
            const auto existing = swiftsearch::StartDate::read(file);
            if(!existing.has_value()){
                throw std::runtime_error(file.string() + " is damaged");
            }
            created = false;
            return *existing;
        }
        const swiftsearch::StartDate fresh = swiftsearch::StartDate::now();
        if(!fresh.write(file)){
            throw std::runtime_error("could not write " + file.string());
        }
        created = true;
        return fresh;
    }

    private:
    fsPath _store_path;
    std::uint64_t _segment_limit;
    std::atomic<std::size_t> _cache_budget; //read without _cache_mtx; setCacheBudget changes it under the lock

    /** @brief SEGMENTS ---------------------------------------------- */
    std::map<std::uint32_t, std::shared_ptr<LockableFile>> _segments; //newest = highest id
    mutable std::shared_mutex _segments_mtx;

    /** @brief LOOKUP ---------------------------------------------- */

    std::mutex _write_mtx; //one writer at a time: append + table update are one step
    swiftsearch::LookupTable _lookup;

    /** @brief CACHE ---------------------------------------------- */
    //which keys are cached in RAM, oldest first. Evicted from the front once over budget
    mutable std::mutex _cache_mtx;
    std::deque<std::pair<std::string, std::size_t>> _cache_fifo;
    std::size_t _cache_bytes = 0;

    /** @brief DEAD BYTES ---------------------------------------------- */
    //per segment, bytes of values no key points at any more: overwritten or deleted. A hint for
    //what is worth compacting, rebuilt by replay. Tombstones are never counted, see compact()
    mutable std::mutex _usage_mtx;
    std::map<std::uint32_t, std::uint64_t> _dead;
    std::mutex _compact_mtx; //one compaction at a time

    /** @brief ACTIVITY ---------------------------------------------- */
    //bumped by every public read and write, so a caller can tell how long the store has been idle
    //and a compaction can tell a request arrived. Compaction's own reads and writes do not count
    std::atomic<std::uint64_t> _activity{0};
    std::atomic<std::chrono::steady_clock::rep> _last_activity{
        std::chrono::steady_clock::now().time_since_epoch().count()};

    /** @brief POOL ---------------------------------------------- */
    mutable std::shared_mutex _pool_mtx; //shared while submitting, exclusive while scaleDownCpu swaps the pool
    std::unique_ptr<swiftsearch::AdaptiveThreadPool> _pool;



    /** @brief MSC ---------------------------------------------------- */
    std::vector<std::string> _warnings; //what replay had to cut off or skip
    bool _start_created = false; //true when this open created store.meta rather than reading it
    swiftsearch::StartDate _secs_since_start; //when the database was first initialised; records store seconds after this
    std::chrono::sys_seconds _start; //the same instant as a time point, so a write does no calendar math

    
    static fsPath createDatabaseFolder(const fsPath& destination){
        if (destination.empty())
            throw std::invalid_argument("createDatabaseFolder: destination path is empty");

        // Throws filesystem_error itself on permission problems, bad paths, etc.
        // Returns false (no error) if the folder already exists.
        std::filesystem::create_directories(destination);

        // Some standard libraries don't report an error when a *file* already sits at
        // the path, so check that what's there is actually a directory.
        if (!std::filesystem::is_directory(destination))
            throw std::filesystem::filesystem_error(
                "createDatabaseFolder: path exists but is not a directory",
                destination,
                std::make_error_code(std::errc::not_a_directory));

        return std::filesystem::absolute(destination);
    }


    /** @attention Segment files ------------------------------------------------ */

    private:

    static std::string segmentFileName(const std::uint32_t id){
        char buf[32];
        std::snprintf(buf, sizeof(buf), "segment-%06u.dat", static_cast<unsigned>(id));
        return buf;
    }

    //"segment-000012.dat" -> 12, anything else -> nullopt
    static std::optional<std::uint32_t> parseSegmentName(const std::string& name){
        constexpr std::string_view prefix = "segment-";
        constexpr std::string_view suffix = ".dat";
        if(name.size() <= prefix.size() + suffix.size()
           || name.compare(0, prefix.size(), prefix) != 0
           || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0){
            return std::nullopt;
        }
        const char* first = name.data() + prefix.size();
        const char* last = name.data() + name.size() - suffix.size();
        std::uint32_t id = 0;
        const auto [ptr, ec] = std::from_chars(first, last, id);
        if(ec != std::errc{} || ptr != last){
            return std::nullopt;
        }
        return id;
    }

    //make the new file's directory entry durable, otherwise a crash can lose the whole file
    void syncStoreDirectory() const{
        const int fd = ::open(_store_path.c_str(), O_RDONLY | O_CLOEXEC);
        if(fd >= 0){
            (void)::fsync(fd);
            ::close(fd);
        }
    }

    std::shared_ptr<LockableFile> createSegment(const std::uint32_t id){
        auto seg = std::make_shared<LockableFile>(_store_path / segmentFileName(id), id);
        syncStoreDirectory();
        std::unique_lock<std::shared_mutex> lock(_segments_mtx);
        _segments.emplace(id, seg);
        return seg;
    }

    std::shared_ptr<LockableFile> segmentById(const std::uint32_t id) const{
        std::shared_lock<std::shared_mutex> lock(_segments_mtx);
        const auto itr = _segments.find(id);
        return itr == _segments.end() ? nullptr : itr->second;
    }

    //the segment the next record goes into, rolling to a new one when this record would push it past the limit.
    //A record bigger than the limit still gets a segment to itself.
    std::shared_ptr<LockableFile> activeSegmentFor(const std::size_t record_size){
        std::shared_ptr<LockableFile> active;
        {
            std::shared_lock<std::shared_mutex> lock(_segments_mtx);
            active = _segments.rbegin()->second;
        }
        const std::uint64_t used = active->size();
        if(used > 0 && used + record_size > _segment_limit){
            return createSegment(active->id() + 1);
        }
        return active;
    }


    /** @attention Startup replay ------------------------------------------------ */

    private:

    //the stop is recorded either way; only the newest segment is ever cut, since a torn tail can only be there
    void endReplayAt(LockableFile& seg, const std::uint64_t at, const swiftsearch::DecodeStatus why, const bool is_newest){
        std::string note = "segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
            + ": " + std::string(swiftsearch::describe(why));
        if(!is_newest){
            note += " (sealed segment, left untouched)";
        }else if(seg.truncate(at)){
            note += " (tail truncated)";
        }else{
            note += " (could not truncate)";
        }
        _warnings.push_back(std::move(note));
    }

    //Header-only probe. Returns the record's total size once the header CRC has passed
    //(peekHeader reports Truncated with at == HEADER_SIZE and a trustworthy `needed`
    //when handed just the header), otherwise the error that stops the walk.
    static std::expected<std::uint64_t, swiftsearch::DecodeError> recordSize(std::span<const std::uint8_t> head){
        const auto parsed = swiftsearch::peekHeader(head);
        if(parsed.has_value()){
            return parsed->totalSize();
        }
        const auto& err = parsed.error();
        if(err.status == swiftsearch::DecodeStatus::Truncated && err.at == swiftsearch::HEADER_SIZE){
            return err.needed;
        }
        return std::unexpected(err);
    }

    //The bytes of the whole record at `at`, read once its header has checked out. The error is the
    //header-level status that stopped it; a record running past `end` is Truncated. The body is not
    //checked here, decodeRecord does that. Shared by replay and compaction.
    //@throws if the file read itself fails
    static std::expected<swiftsearch::Bytes, swiftsearch::DecodeStatus> readWholeRecord(const LockableFile& seg,
        const std::uint64_t at, const std::uint64_t end){
        const std::uint64_t remaining = end - at;
        const auto head = seg.readAt(at, std::min<std::uint64_t>(swiftsearch::HEADER_SIZE, remaining));
        if(!head.has_value()){
            throw std::runtime_error("read failed in segment " + std::to_string(seg.id()));
        }
        const auto total = recordSize(*head);
        if(!total.has_value()){
            return std::unexpected(total.error().status); //torn, not a record, header rot, or policy
        }
        if(*total > remaining){
            return std::unexpected(swiftsearch::DecodeStatus::Truncated); //header fine, body never finished
        }
        auto whole = seg.readAt(at, *total);
        if(!whole.has_value()){
            throw std::runtime_error("read failed in segment " + std::to_string(seg.id()));
        }
        return std::move(*whole);
    }

    void replaySegment(LockableFile& seg, const bool is_newest){
        using swiftsearch::DecodeStatus;
        const std::uint64_t end = seg.size();
        std::uint64_t at = 0;

        while(at < end){
            const auto whole = readWholeRecord(seg, at, end);
            if(!whole.has_value()){
                if(swiftsearch::isPolicy(whole.error())){
                    //intact bytes this build will not interpret: refuse to start rather than cut data away
                    throw std::runtime_error("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": " + std::string(swiftsearch::describe(whole.error())));
                }
                endReplayAt(seg, at, whole.error(), is_newest);
                return;
            }
            const std::uint64_t total = whole->size();
            const swiftsearch::DiskLocation here{seg.id(), at, total};

            const auto decoded = swiftsearch::decodeRecord(*whole);
            if(!decoded.has_value()){
                const DecodeStatus status = decoded.error().status;
                if(swiftsearch::isPolicy(status)){
                    throw std::runtime_error("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": " + std::string(swiftsearch::describe(status)));
                }
                if(status == DecodeStatus::BodyCrcMismatch && !(is_newest && at + total == end)){
                    //the header was sound so the length is too: skip this one record and keep the rest.
                    //The key is untrusted (only the body CRC covered it), so it is not named here.
                    _warnings.push_back("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": record body is damaged, skipped");
                    markDead(here);
                    at += total;
                    continue;
                }
                endReplayAt(seg, at, status, is_newest); //includes a bad record that is the very last write
                return;
            }

            //the same bookkeeping Put and Delete do, so the dead counts come back after a restart
            const auto& envelope = decoded->envelope;
            const auto before = _lookup.lookup(envelope.key());
            if(envelope.isTombstone()){
                _lookup.erase(envelope.key());
                markReplaced(before);
            }else if(_lookup.putDisk(envelope.key(), here, envelope.header().var_type)){
                markReplaced(before);
            }else{
                _warnings.push_back("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                    + ": record with an empty key, skipped");
                markDead(here);
            }
            at += total;
        }
    }

    void replay(){
        std::map<std::uint32_t, fsPath> found;
        for(const auto& entry : std::filesystem::directory_iterator(_store_path)){
            if(!entry.is_regular_file()){
                continue;
            }
            const auto id = parseSegmentName(entry.path().filename().string());
            if(id.has_value()){
                found.emplace(*id, entry.path());
            }
        }

        if(_start_created && !found.empty()){
            _warnings.push_back(std::string(META_FILE) + " was missing, so the start date was reset to now; "
                "timestamps already in the segments are relative to the lost one");
        }
        if(found.empty()){
            createSegment(0);
        }
        for(const auto& [id, path] : found){
            _segments.emplace(id, std::make_shared<LockableFile>(path, id));
        }

        const std::uint32_t newest = _segments.rbegin()->first;
        for(const auto& [id, seg] : _segments){
            replaySegment(*seg, id == newest);
        }
    }


    /** @attention Private helpers --------------------------------------------- */

    private:

    /** @attention Dead bytes and activity ------------------------------------- */

    void markDead(const swiftsearch::DiskLocation& where){
        std::lock_guard<std::mutex> lock(_usage_mtx);
        _dead[where.segment] += where.length;
    }

    //a write replaced or removed the entry a key had: its record is dead weight in its segment now
    void markReplaced(const std::optional<swiftsearch::Entry>& before){
        if(before.has_value() && before->onDisk()){
            markDead(*before->disk);
        }
    }

    void noteActivity() noexcept{
        _activity.fetch_add(1);
        _last_activity.store(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    //true when `key` no longer lives at `was`: compaction moved it, or a write replaced it
    bool movedSince(const std::string& key, const swiftsearch::DiskLocation& was) const{
        const auto now = _lookup.lookup(key);
        return now.has_value() && (now->inMemory() || now->disk != was);
    }

    //true when the table says `key`'s live value is the record at `where`
    bool pointsAt(const std::string& key, const swiftsearch::DiskLocation& where) const{
        const auto entry = _lookup.lookup(key);
        return entry.has_value() && entry->disk == where;
    }


    /** @attention Compaction ------------------------------------------------ */

    //one live value or needed tombstone, read from the segment being compacted and waiting to be copied
    struct Pending{
        std::string key;
        swiftsearch::DiskLocation from;
        swiftsearch::Bytes raw; //the record exactly as it is on disk, timestamp and all
        bool tombstone = false;
    };

    enum class SegmentOutcome{ Removed, Kept, Stopped };

    //Segments worth rewriting, oldest first: at least min_dead_percent of their bytes dead, or
    //sealed and empty. When the active segment is one of them a new active segment is started, so
    //the old one is sealed and nothing writes into it while it is being compacted
    std::vector<std::shared_ptr<LockableFile>> compactionCandidates(const std::size_t min_dead_percent){
        std::map<std::uint32_t, std::uint64_t> dead;
        {
            std::lock_guard<std::mutex> lock(_usage_mtx);
            dead = _dead;
        }
        std::vector<std::shared_ptr<LockableFile>> picked;
        std::shared_ptr<LockableFile> active;
        {
            std::shared_lock<std::shared_mutex> lock(_segments_mtx);
            active = _segments.rbegin()->second;
            for(const auto& [id, seg] : _segments){
                const std::uint64_t size = seg->size();
                const auto itr = dead.find(id);
                const std::uint64_t dead_bytes = itr == dead.end() ? 0 : itr->second;
                if(size == 0 ? seg != active : dead_bytes * 100 >= min_dead_percent * size){
                    picked.push_back(seg);
                }
            }
        }
        if(!picked.empty() && picked.back() == active){
            std::lock_guard<std::mutex> lock(_write_mtx);
            std::uint32_t newest = 0;
            {
                std::shared_lock<std::shared_mutex> segs(_segments_mtx);
                newest = _segments.rbegin()->first;
            }
            if(newest == active->id()){ //a write may have rolled past it already, which seals it just the same
                createSegment(active->id() + 1);
            }
        }
        return picked;
    }

    //compaction's read of one record: every CRC checked. The error is a sentence for the report
    static std::expected<std::pair<swiftsearch::Bytes, swiftsearch::Envelope>, std::string>
    checkedRecordAt(const LockableFile& seg, const std::uint64_t at, const std::uint64_t end){
        try{
            auto whole = readWholeRecord(seg, at, end);
            if(!whole.has_value()){
                return std::unexpected(std::string(swiftsearch::describe(whole.error())));
            }
            auto decoded = swiftsearch::decodeRecord(*whole);
            if(!decoded.has_value()){
                return std::unexpected(std::string(swiftsearch::describe(decoded.error().status)));
            }
            return std::pair{std::move(*whole), std::move(decoded->envelope)};
        }catch(const std::exception& e){
            return std::unexpected(std::string(e.what()));
        }
    }

    //A first look, without the write lock, at whether a record has to survive. copyForward asks
    //again under the lock before anything is written, so a stale answer here only costs a skip
    bool worthCopying(const swiftsearch::Envelope& envelope, const swiftsearch::DiskLocation& here,
                      const bool oldest) const{
        if(envelope.isTombstone()){
            //in the oldest segment nothing older is left for the key to come back from. Elsewhere it
            //is still needed while the key stays deleted; once the key is back, a later write won
            return !oldest && !_lookup.contains(envelope.key());
        }
        return pointsAt(envelope.key(), here);
    }

    //Re-checks every pending record under the write lock, appends the ones still needed to the
    //active segment in one write and one fsync, and points the table at the copies.
    //Checking and appending under the same lock is what keeps this right: a client write that
    //slipped in between would otherwise be followed by an older copy, and replay would bring the
    //old value (or the deletion) back
    Error copyForward(const std::vector<Pending>& batch, swiftsearch::CompactReport& report){
        std::lock_guard<std::mutex> lock(_write_mtx);
        swiftsearch::Bytes out;
        std::vector<const Pending*> kept;
        for(const auto& p : batch){
            if(p.tombstone ? !_lookup.contains(p.key) : pointsAt(p.key, p.from)){
                out.insert(out.end(), p.raw.begin(), p.raw.end());
                kept.push_back(&p);
            }
        }
        if(kept.empty()){
            return GOOD;
        }
        const auto seg = activeSegmentFor(out.size());
        const auto at = seg->append(out);
        if(!at.has_value()){
            return BAD("could not copy records forward: " + at.error());
        }
        std::uint64_t offset = *at;
        for(const Pending* p : kept){
            const swiftsearch::DiskLocation to{seg->id(), offset, p->raw.size()};
            offset += p->raw.size();
            if(p->tombstone){
                continue; //nothing in the table points at a tombstone
            }
            //the original is garbage from here on, which matters if this run stops before its segment goes
            markDead(_lookup.relocate(p->key, p->from, to) ? p->from : to);
        }
        report.bytesCopied += out.size();
        return GOOD;
    }

    //Takes a segment compaction has emptied out of the segment table, so no new reader finds it,
    //then off the disk. A reader still holding it keeps its open descriptor until it is done
    void removeSegment(const std::shared_ptr<LockableFile>& seg, swiftsearch::CompactReport& report){
        {
            std::unique_lock<std::shared_mutex> lock(_segments_mtx);
            _segments.erase(seg->id());
        }
        {
            std::lock_guard<std::mutex> lock(_usage_mtx);
            _dead.erase(seg->id());
        }
        std::error_code ec;
        std::filesystem::remove(seg->path(), ec);
        if(ec){
            //everything in it is a duplicate or dead, so a restart that finds it again loses nothing
            report.problems.push_back("could not delete " + seg->path().string() + ": " + ec.message());
        }
        syncStoreDirectory();
        report.segmentsRemoved += 1;
        report.bytesRemoved += seg->size();
    }

    //Walks one sealed segment, copies what must survive forward a batch at a time, then deletes it.
    //A record that will not read keeps the whole segment: compaction never deletes what it could not check
    std::expected<SegmentOutcome, std::string> compactSegment(const std::shared_ptr<LockableFile>& seg,
        const std::function<bool()>& stop, swiftsearch::CompactReport& report){
        bool oldest = false;
        {
            std::shared_lock<std::shared_mutex> lock(_segments_mtx);
            oldest = _segments.begin()->first == seg->id();
        }
        const std::uint64_t end = seg->size();
        //a quarter segment at most, so even a segment under 4 MiB gives way to a request part way through
        const std::uint64_t batch_limit = std::max<std::uint64_t>(1, std::min(COMPACT_BATCH_BYTES, _segment_limit / 4));
        std::vector<Pending> batch;
        std::uint64_t walked = 0; //bytes looked at since the last flush. Counting dead ones too means a
                                  //mostly dead segment still gives way to a request every batch
        std::uint64_t at = 0;

        while(at < end){
            auto record = checkedRecordAt(*seg, at, end);
            if(!record.has_value()){
                report.problems.push_back("segment " + std::to_string(seg->id()) + " offset " + std::to_string(at)
                    + ": " + record.error() + " (segment kept)");
                return SegmentOutcome::Kept;
            }
            auto& [raw, envelope] = *record;
            const swiftsearch::DiskLocation here{seg->id(), at, raw.size()};
            at += raw.size();
            walked += raw.size();
            if(worthCopying(envelope, here, oldest)){
                batch.push_back(Pending{envelope.key(), here, std::move(raw), envelope.isTombstone()});
            }
            if(walked >= batch_limit){
                if(const auto e = copyForward(batch, report)){
                    return std::unexpected(*e);
                }
                batch.clear();
                walked = 0;
                if(at < end && stop()){
                    return SegmentOutcome::Stopped;
                }
            }
        }
        if(const auto e = copyForward(batch, report)){
            return std::unexpected(*e);
        }
        removeSegment(seg, report);
        return SegmentOutcome::Removed;
    }


    //reads one record's value from the segment file named by where
    std::optional<swiftsearch::Bytes> find_and_read(const std::string& key, const swiftsearch::DiskLocation& where){
        const auto seg = segmentById(where.segment);
        if(seg == nullptr){
            return std::nullopt;
        }
        const auto raw = seg->readAt(where.offset, where.length);
        if(!raw.has_value()){
            return std::nullopt;
        }
        const auto decoded = swiftsearch::decodeRecord(*raw); //verifies both CRCs
        if(!decoded.has_value()){
            return std::nullopt;
        }
        const auto& envelope = decoded->envelope;
        if(envelope.isTombstone() || envelope.key() != key){
            return std::nullopt; //the table pointed at the wrong record
        }
        const auto payload = envelope.payload();
        return swiftsearch::Bytes(payload.begin(), payload.end());
    }


    /**
     * Call this only AFTER the record is appended to a segment and fsync'd.
     * The table lives in RAM and is rebuilt from the segments on startup, so an
     * entry that points at bytes which never reached disk is a lie that a crash
     * exposes. See the CRASH BEHAVIOR note in find.hpp.
     */
    Error putInLookupTable(const std::string& name, const swiftsearch::DiskLocation& where, const VAR_TYPE type){
        try{
            if(!_lookup.putDisk(name, where, type)){
                return BAD("empty keys cannot be stored");
            }
        }catch(const std::exception& e){
            return BAD("error entering in lookup table");
        }
        return GOOD;
    }



    std::optional<swiftsearch::Entry> location_in_lookup(const std::string_view& looking){
        return _lookup.lookup(looking);
    }


    //caller holds _cache_mtx. Pops the oldest cached keys into `victims` until the cache fits its
    //budget, leaving at least `keep` entries. The caller evicts the victims after letting go of the lock
    void evictOverBudgetLocked(std::vector<std::string>& victims, const std::size_t keep){
        const std::size_t budget = _cache_budget.load();
        while(_cache_bytes > budget && _cache_fifo.size() > keep){
            _cache_bytes -= _cache_fifo.front().second;
            victims.push_back(std::move(_cache_fifo.front().first));
            _cache_fifo.pop_front();
        }
    }


    //note a key was just cached and push the oldest cached keys out once over budget
    void rememberCached(const std::string& key, const std::size_t bytes){
        std::vector<std::string> victims;
        {
            std::lock_guard<std::mutex> lock(_cache_mtx);
            _cache_fifo.emplace_back(key, bytes);
            _cache_bytes += bytes;
            evictOverBudgetLocked(victims, 1); //never the value that just arrived
        }
        for(const auto& victim : victims){
            _lookup.evict(victim);
        }
    }


    //a record's payload size from its length in the table: whatever is not header, key or trailer
    static std::uint64_t payloadSize(const std::size_t key_size, const swiftsearch::DiskLocation& where){
        const std::uint64_t fixed = swiftsearch::RECORD_OVERHEAD + key_size;
        return where.length > fixed ? where.length - fixed : 0;
    }


    std::optional<Value> thread_gets(const std::string& item){
        for(int attempt = 1; attempt <= READ_ATTEMPTS; ++attempt){
            const auto entry = location_in_lookup(item);
            if(!entry.has_value()){
                return std::nullopt;
            }
            if(entry->inMemory()){
                return Value{*entry->memory, entry->type}; //copy out; the shared buffer stays in the table
            }
            if(!entry->onDisk()){
                return std::nullopt;
            }

            auto bytes = find_and_read(item, *entry->disk);
            if(bytes.has_value()){
                cacheLoaded(item, *entry->disk, *bytes);
                return Value{std::move(*bytes), entry->type};
            }
            //compaction can move the record (and delete its old segment) between the lookup and the
            //read, so look again. If it has not moved, the key is known but its record will not read
            if(!movedSince(item, *entry->disk)){
                return std::nullopt;
            }
        }
        return std::nullopt;
    }


    //puts a value just read from `where` into the in-memory cache and counts it against the budget.
    //@return false if it did not go in: too big for the budget, already cached, or the key was
    //overwritten or relocated while it was being read (all harmless, the caller still has the bytes)
    bool cacheLoaded(const std::string& key, const swiftsearch::DiskLocation& where, const swiftsearch::Bytes& bytes){
        if(bytes.size() > _cache_budget.load()){
            return false;
        }
        if(!_lookup.cache(key, where, std::make_shared<const swiftsearch::Bytes>(bytes))){
            return false;
        }
        rememberCached(key, bytes.size());
        return true;
    }


    //the work of prepare(): get one key's value from disk into the cache, or say why it could not
    Error load_into_memory(const std::string& key){
        for(int attempt = 1; ; ++attempt){
            const auto entry = location_in_lookup(key);
            if(!entry.has_value()){
                return BAD("no such key");
            }
            if(entry->inMemory()){
                return GOOD; //nothing to do. putMemory'd values and already cached ones end up here
            }
            if(!entry->onDisk()){
                return BAD("key has no stored value");
            }

            //the payload size is exact from the table, so an impossible request fails before any disk read
            if(payloadSize(key.size(), *entry->disk) > _cache_budget.load()){
                return BAD("value is larger than the memory budget");
            }

            const auto bytes = find_and_read(key, *entry->disk);
            if(bytes.has_value() && cacheLoaded(key, *entry->disk, *bytes)){
                return GOOD;
            }
            //cacheLoaded refuses when another reader cached it first, which is fine, or when the key
            //changed under us. Compaction moving the record, or a write replacing it, is worth
            //another look; a record that will not read where it still is, is the answer
            const auto now = location_in_lookup(key);
            if(now.has_value() && now->inMemory()){
                return GOOD;
            }
            if(attempt < READ_ATTEMPTS && movedSince(key, *entry->disk)){
                continue;
            }
            return BAD(bytes.has_value() ? "the key changed while it was being loaded, try again"
                                         : "could not read the value from disk");
        }
    }


    //whole seconds since the store was first initialised: what each record carries as its timestamp.
    //A clock set back before the start date reads as 0. nullopt once the count no longer fits in 32 bits (~136 years)
    std::optional<std::uint32_t> secondsSinceStart() const{
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        if(now <= _start){
            return 0;
        }
        const auto elapsed = (now - _start).count();
        if(elapsed > static_cast<decltype(elapsed)>(std::numeric_limits<std::uint32_t>::max())){
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(elapsed);
    }


    //caller holds _write_mtx. Appends the record durably and returns where it landed
    std::expected<swiftsearch::DiskLocation, std::string> appendLocked(const swiftsearch::Bytes& record){
        const auto seg = activeSegmentFor(record.size());
        const auto at = seg->append(record);
        if(!at.has_value()){
            return std::unexpected(at.error());
        }
        return swiftsearch::DiskLocation{seg->id(), *at, record.size()};
    }



    public:
    /**
     * Opens (creating if needed) the store in `path` and replays its segments.
     * @throws if the folder cannot be made, or the segments cannot be read, or
     * they hold records this build refuses to interpret (nothing is deleted then).
     * Damage replay can recover from (a torn tail, a rotten record) is not an
     * error; it is listed by Warnings().
     */
    explicit SwiftSearch(const fsPath& path, const std::uint64_t segment_limit = DEFAULT_SEGMENT_LIMIT,
    const std::size_t cache_budget = DEFAULT_CACHE_BUDGET): 
    _store_path(createDatabaseFolder(path)),
    _segment_limit(segment_limit), 
    _cache_budget(cache_budget), 
    _pool(std::make_unique<swiftsearch::AdaptiveThreadPool>()),
    _secs_since_start(openStartDate(_store_path, _start_created)),
    _start(*_secs_since_start.toTimePoint()){ //openStartDate only returns dates that have one
        try{
            replay();
        }catch(const std::exception& e){
            throw std::runtime_error("SwiftSearch Failed to Setup In constructor ");
        }
    }


    /* BEGIN PUBLIC METHODS ----------------------------------------------------------- */

    /// Runs the read on the pool and waits for it. Don't call this from inside a pool task.
    std::optional<swiftsearch::Bytes> Get(const std::string_view& item){
        auto value = GetTyped(item);
        if(!value.has_value()){
            return std::nullopt;
        }
        return std::move(value->bytes);
    }

    /// Get, plus the type the value was stored as. Same pool rule as Get.
    std::optional<Value> GetTyped(const std::string_view& item){
        noteActivity();
        std::future<std::optional<Value>> answer;
        {
            std::shared_lock<std::shared_mutex> lock(_pool_mtx);
            answer = _pool->submit([this, key = std::string(item)]{ return thread_gets(key); });
        }
        return answer.get();
    }

    /// Durable when this returns GOOD: the record is on disk and fsync'd.
    /// Takes the value by value so a caller that is done with its buffer can move it in.
    Error Put(const std::string& name, swiftsearch::Bytes _data,
              const VAR_TYPE type = VAR_TYPE::BIN){
        noteActivity();
        if(!swiftsearch::keys::is_valid(name)){
            return BAD("invalid key");
        }
        try{
            const auto secs = secondsSinceStart();
            if(!secs.has_value()){
                return BAD("the store is more than 136 years past its start date");
            }
            const auto record = swiftsearch::Envelope::value(name, std::move(_data), *secs, type).encode();

            std::lock_guard<std::mutex> lock(_write_mtx);
            const auto before = _lookup.lookup(name); //exact: every writer and every relocate holds this lock
            const auto where = appendLocked(record);
            if(!where.has_value()){
                return BAD(where.error());
            }
            if(const auto e = putInLookupTable(name, *where, type)){
                return e;
            }
            markReplaced(before);
            return GOOD;
        }catch(const std::length_error&){
            return BAD("key or value is over the size limit");
        }catch(const std::exception& e){
            return BAD(std::string("file failed to store: ") + e.what());
        }
    }

    /// Deleting a key that does not exist is not an error. Durable when this returns GOOD.
    Error Delete(const std::string_view& item){
        noteActivity();
        const std::string key(item);
        try{
            std::lock_guard<std::mutex> lock(_write_mtx);
            const auto before = _lookup.lookup(key);
            if(!before.has_value()){
                return GOOD;
            }
            //tombstone first, forget second: forgetting alone would let a restart bring the key back
            const auto secs = secondsSinceStart();
            if(!secs.has_value()){
                return BAD("the store is more than 136 years past its start date");
            }
            const auto record = swiftsearch::Envelope::tombstone(key, *secs).encode();
            const auto where = appendLocked(record);
            if(!where.has_value()){
                return BAD(where.error());
            }
            _lookup.erase(key);
            markReplaced(before); //the value is dead now. The tombstone is not counted, see compact()
        }catch(const std::exception& e){
            return BAD(std::string("delete failed: ") + e.what());
        }
        return GOOD;
    }


    /**
     * Moves one key's value from disk into memory, so the Get after it is served from RAM.
     *
     * "Moves" is a copy: the log is append-only, so the record stays on disk and the key
     * becomes Residency::Both. The cached copy counts against the cache budget like any
     * other and is NOT pinned, so later reads can push it out again (oldest cached first).
     *
     * GOOD when the value is in memory afterwards, including when it already was.
     * BAD when the key does not exist, the value is bigger than the whole budget, the
     * disk read fails (bad CRC, missing segment), or the key was overwritten mid-load.
     * Runs on the pool like Get does. Don't call this from inside a pool task.
     */
    Error prepare(const std::string_view& item){
        noteActivity();
        try{
            std::future<Error> answer;
            {
                std::shared_lock<std::shared_mutex> lock(_pool_mtx);
                answer = _pool->submit([this, key = std::string(item)]{
                     return load_into_memory(key); 
                    });
            }
            return answer.get();
        }catch(const std::exception& e){
            return BAD(std::string("prepare failed: ") + e.what());
        }
    }


    /**
     * Retunes how hard the pool works the CPU: it backs off at `hi` percent busy and
     * ramps up again below `low`. Keeps the current thread count. Takes effect by
     * replacing the pool, which finishes the reads already queued first.
     */
    Error scaleDownCpu(const std::size_t& hi, const std::size_t& low){
        return setCpuLimits(cpuThreads(), hi, low);
    }


    /**
     * Caps the pool at `max_threads` workers (0 = one per hardware thread) and sets the
     * busy percentages it backs off at (`hi`) and ramps up below (`low`). The thread cap
     * holds on every platform; the percentages only act where the pool can read CPU usage,
     * which is Linux (/proc/stat). Takes effect by replacing the pool, like scaleDownCpu.
     */
    Error setCpuLimits(const std::size_t max_threads, const std::size_t& hi, const std::size_t& low){
        if(hi <= low || hi > 100){
            return BAD("values for scaling are out of range");
        }
        try{
            swiftsearch::ThreadPoolOptions options;
            options.maxThreads = max_threads;
            options.highBusyPercent = static_cast<double>(hi);
            options.lowBusyPercent = static_cast<double>(low);
            auto fresh = std::make_unique<swiftsearch::AdaptiveThreadPool>(options);
            {
                std::unique_lock<std::shared_mutex> lock(_pool_mtx);
                _pool.swap(fresh);
            }
            //`fresh` now holds the old pool; its destructor drains it, outside the lock
        }catch(const std::exception& e){
            return BAD(std::string("could not rescale: ") + e.what());
        }
        return GOOD;
    }


    /**
     * Where a key's value is right now: Memory (written but not flushed), Disk (on disk
     * only) or Both (on disk with a cached copy, e.g. after prepare() or a Get).
     * nullopt if the key does not exist. A snapshot: a concurrent Get or eviction can
     * change the answer a moment later.
     */
    std::optional<swiftsearch::Residency> residency(const std::string_view& key) const{
        const auto entry = _lookup.lookup(key);
        if(!entry.has_value()){
            return std::nullopt;
        }
        return entry->residency();
    }

    /// Worker threads in the pool right now (the resolved count, never 0).
    std::size_t cpuThreads() const{
        std::shared_lock<std::shared_mutex> lock(_pool_mtx);
        return _pool->maxThreads();
    }


    /**
     * Changes how many bytes of cached values the store may hold. If the cache is over the
     * new budget the oldest cached values are dropped right away. 0 turns caching off.
     * Only cached copies are dropped: every value stays on disk, nothing is lost.
     */
    void setCacheBudget(const std::size_t bytes){
        std::vector<std::string> victims;
        {
            std::lock_guard<std::mutex> lock(_cache_mtx);
            _cache_budget.store(bytes);
            evictOverBudgetLocked(victims, 0);
        }
        for(const auto& victim : victims){
            _lookup.evict(victim);
        }
    }

    std::size_t cacheBudget() const noexcept{
        return _cache_budget.load();
    }

    /// Bytes counted against the cache budget. Can briefly include values that were since
    /// overwritten, until they reach the front of the queue and are dropped.
    std::size_t cacheBytes() const{
        std::lock_guard<std::mutex> lock(_cache_mtx);
        return _cache_bytes;
    }


    /// Size of a key's value in bytes, taken from the table, so nothing is read from disk.
    /// nullopt if the key does not exist.
    std::optional<std::uint64_t> valueSize(const std::string_view& key) const{
        const auto entry = _lookup.lookup(key);
        if(!entry.has_value()){
            return std::nullopt;
        }
        if(entry->inMemory()){
            return entry->memory->size();
        }
        if(!entry->onDisk()){
            return std::nullopt;
        }
        return payloadSize(key.size(), *entry->disk);
    }


    /**
     * Gives back the disk space that overwritten and deleted values hold.
     *
     * Rewrites every segment where at least `min_dead_percent` of the bytes are dead, oldest
     * first; 0 rewrites every segment there is, dead bytes or not. Each live record is copied,
     * byte for byte, to the end of the active segment and its table entry moved there; then
     * the old file is deleted. If the active segment qualifies, a new one is started first so
     * it can be compacted like the rest.
     *
     * CRASH SAFETY: a copy always lands in a newer segment than its original, so replay
     * applies it later and the last record for the key still wins, with the same value.
     * A crash part way leaves duplicates, which replay counts as dead and the next run
     * clears. There is never a moment when the only copy is in an unfinished file.
     *
     * TOMBSTONES are dropped when their segment is the oldest on disk, since nothing older
     * is left for the key to come back from. Elsewhere one whose key is still deleted is
     * carried forward like a live record. They are never counted as dead bytes, so a
     * segment of carried tombstones does not qualify again every run.
     *
     * `should_stop` is asked before each segment and after each batch of about 4 MiB;
     * true ends the run with report.stopped set. What was done stays done.
     *
     * A segment holding a record that will not read is kept whole and named in
     * report.problems. Errors: bad percent, another compaction running, or a failed write.
     */
    std::expected<swiftsearch::CompactReport, std::string> compact(const std::size_t min_dead_percent,
        const std::function<bool()>& should_stop = {}){
        if(min_dead_percent > 100){
            return std::unexpected("dead percent must be 0 to 100");
        }
        std::unique_lock<std::mutex> running(_compact_mtx, std::try_to_lock);
        if(!running.owns_lock()){
            return std::unexpected("a compaction is already running");
        }
        const std::function<bool()> stop = [&should_stop]{ return should_stop && should_stop(); };

        swiftsearch::CompactReport report;
        try{
            for(const auto& seg : compactionCandidates(min_dead_percent)){
                if(stop()){
                    report.stopped = true;
                    break;
                }
                const auto outcome = compactSegment(seg, stop, report);
                if(!outcome.has_value()){
                    return std::unexpected(outcome.error());
                }
                if(*outcome == SegmentOutcome::Stopped){
                    report.stopped = true;
                    break;
                }
            }
        }catch(const std::exception& e){
            return std::unexpected(std::string("compaction failed: ") + e.what());
        }
        return report;
    }


    /// Bytes of overwritten and deleted values still on disk: the counter compaction goes by.
    std::uint64_t deadBytes() const{
        std::lock_guard<std::mutex> lock(_usage_mtx);
        std::uint64_t sum = 0;
        for(const auto& [id, dead] : _dead){
            sum += dead;
        }
        return sum;
    }

    /// Bytes in every segment file, live and dead.
    std::uint64_t diskBytes() const{
        std::shared_lock<std::shared_mutex> lock(_segments_mtx);
        std::uint64_t sum = 0;
        for(const auto& [id, seg] : _segments){
            sum += seg->size();
        }
        return sum;
    }

    std::size_t segmentCount() const{
        std::shared_lock<std::shared_mutex> lock(_segments_mtx);
        return _segments.size();
    }

    /// Goes up by one on every Get, Put, Delete and prepare. Compaction's own work does not move it.
    std::uint64_t activityCount() const noexcept{
        return _activity.load();
    }

    /// When the store last did a Get, Put, Delete or prepare; when it was opened if never.
    std::chrono::steady_clock::time_point lastActivity() const noexcept{
        return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(_last_activity.load()));
    }


    /// Number of live keys.
    std::size_t Size() const{
        return _lookup.size();
    }

    /// What startup replay cut off or skipped. Empty after a clean shutdown.
    const std::vector<std::string>& Warnings() const noexcept{
        return _warnings;
    }

};
