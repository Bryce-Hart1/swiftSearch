#include "bstd/store/serialize.hpp"
#include "bytes.hpp"
#include "crc32c.hpp"
#include "find.hpp"
#include "record.hpp"
#include "threading.hpp"
#include <algorithm>
#include <array>
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


    private:

    //push a descriptor's data to stable storage. fsync alone does not flush the drive's cache on macOS
    static bool syncDescriptor(const int fd){
#ifdef F_FULLFSYNC
        if(::fcntl(fd, F_FULLFSYNC) == 0){
            return true;
        }
#endif
        return ::fsync(fd) == 0;
    }

    //write-to-temp, fsync, rename over `file`, fsync the folder: after a crash `file` is the old
    //content or the new content, never a mixture
    static bool writeFileAtomically(const fsPath& file, std::span<const std::uint8_t> bytes){
        const fsPath temp = file.string() + ".tmp";
        const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if(fd < 0){
            return false;
        }
        bool ok = true;
        std::size_t done = 0;
        while(ok && done < bytes.size()){
            const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
            if(n < 0){
                ok = (errno == EINTR);
            }else{
                done += static_cast<std::size_t>(n);
            }
        }
        ok = ok && syncDescriptor(fd);
        ::close(fd);
        if(!ok || ::rename(temp.c_str(), file.c_str()) != 0){
            ::unlink(temp.c_str());
            return false;
        }
        const int dir = ::open(file.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
        if(dir >= 0){
            (void)::fsync(dir);
            ::close(dir);
        }
        return true;
    }

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
            return syncDescriptor(_fd);
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

    /**
     * The instant the store was first initialised, as UTC calendar fields. A record
     * does not store a date at all, only how many seconds after this instant it was
     * written (RecordHeader::secs_since_start), which is what keeps it to 4 bytes.
     *
     * It lives in its own small binary file, store.meta, in the data folder. It is
     * written once, atomically, the first time the store is opened, and never
     * changes after that.
     *
     * File layout (integers little-endian):
     *   off  size  field
     *     0     4  magic    53 53 4D 1A  ('S' 'S' 'M' 0x1A)
     *     4     1  version  1
     *     5     1  length   bytes of fields below (11)
     *     6    11  fields   second, minute, hour, day, month (a bin8 each, 10 bits) then
     *                       year (a bin32, 34 bits); 84 bits packed MSB-first, the last
     *                       byte padded with 0 bits
     *    17     4  crc32c   over bytes [0, 17)
     *
     * A wrong start date would skew every timestamp without anything noticing, so a
     * file that fails its CRC or holds an impossible date is refused, not guessed at.
     *
     * There is no default constructor: bin8 and bin32 have none.
     */
    struct StartDate{
        using bin8 = bstd::store::bin8;
        using bin32 = bstd::store::bin32;
        using sys_seconds = std::chrono::sys_seconds;

        static constexpr std::uint32_t MAGIC = 0x1A4D5353u; //on disk 53 53 4D 1A
        static constexpr std::uint8_t VERSION = 1;
        static constexpr std::size_t HEADER_BYTES = 6; //magic + version + length
        static constexpr std::size_t FIELD_BITS = 5 * bin8::bit_width + bin32::bit_width; //84
        static constexpr std::size_t FIELD_BYTES = (FIELD_BITS + 7) / 8; //11
        static constexpr std::size_t FILE_BYTES = HEADER_BYTES + FIELD_BYTES + 4; //21

        bin8 second;
        bin8 minute;
        bin8 hour;
        bin8 day;
        bin8 month;
        bin32 year;

        StartDate(const std::uint8_t s, const std::uint8_t mi, const std::uint8_t h,
                  const std::uint8_t d, const std::uint8_t mo, const std::uint32_t y)
            : second(s), minute(mi), hour(h), day(d), month(mo), year(y){}

        StartDate(const bin8 s, const bin8 mi, const bin8 h, const bin8 d, const bin8 mo, const bin32 y)
            : second(s), minute(mi), hour(h), day(d), month(mo), year(y){}

        /** @attention Capturing a date ---------------------------------------- */

        /// The UTC calendar fields of `tp`.
        static StartDate fromTimePoint(const sys_seconds tp){
            namespace chr = std::chrono;
            const chr::sys_days today = chr::floor<chr::days>(tp);
            const chr::year_month_day ymd{today};
            const chr::hh_mm_ss<chr::seconds> hms{tp - today};
            return StartDate(static_cast<std::uint8_t>(hms.seconds().count()),
                             static_cast<std::uint8_t>(hms.minutes().count()),
                             static_cast<std::uint8_t>(hms.hours().count()),
                             static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day())),
                             static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month())),
                             static_cast<std::uint32_t>(static_cast<int>(ymd.year())));
        }

        /// The current UTC time, to the second.
        static StartDate now(){
            namespace chr = std::chrono;
            return fromTimePoint(chr::floor<chr::seconds>(chr::system_clock::now()));
        }

        /** @attention Reading it back ------------------------------------------ */

        //nullopt when the bin is not the type this struct always writes into it (only a damaged file does that)
        static std::optional<std::uint8_t> asU8(const bin8& b) noexcept{
            const auto v = b.value();
            if(const auto* p = std::get_if<std::uint8_t>(&v)){
                return *p;
            }
            return std::nullopt;
        }

        static std::optional<std::uint32_t> asU32(const bin32& b) noexcept{
            const auto v = b.value();
            if(const auto* p = std::get_if<std::uint32_t>(&v)){
                return *p;
            }
            return std::nullopt;
        }

        /// {year, month, day, hour, minute, second}. decode() has already refused any
        /// file whose fields are not valid, so a StartDate you hold always has real values.
        std::array<int, 6> values() const{
            return {static_cast<int>(asU32(year).value_or(0)),
                    asU8(month).value_or(0),
                    asU8(day).value_or(0),
                    asU8(hour).value_or(0),
                    asU8(minute).value_or(0),
                    asU8(second).value_or(0)};
        }

        /// This date as a point in time, or nullopt if the fields are not a real UTC date and time.
        std::optional<sys_seconds> toTimePoint() const{
            namespace chr = std::chrono;
            const auto s = asU8(second);
            const auto mi = asU8(minute);
            const auto h = asU8(hour);
            const auto d = asU8(day);
            const auto mo = asU8(month);
            const auto y = asU32(year);
            if(!s || !mi || !h || !d || !mo || !y){
                return std::nullopt;
            }
            if(*s > 59 || *mi > 59 || *h > 23 || *y > 9999){
                return std::nullopt;
            }
            const chr::year_month_day ymd{chr::year{static_cast<int>(*y)}, chr::month{*mo}, chr::day{*d}};
            if(!ymd.ok()){
                return std::nullopt;
            }
            return sys_seconds{chr::sys_days{ymd} + chr::hours{*h} + chr::minutes{*mi} + chr::seconds{*s}};
        }

        /** @attention The metadata file ---------------------------------------- */

        /// The whole file's bytes: header, packed fields, CRC.
        swiftsearch::Bytes encode() const{
            bstd::bit::BitBuffer bits;
            second.write_bits(bits);
            minute.write_bits(bits);
            hour.write_bits(bits);
            day.write_bits(bits);
            month.write_bits(bits);
            year.write_bits(bits);

            swiftsearch::Bytes out;
            out.reserve(FILE_BYTES);
            swiftsearch::put_u32_le(out, MAGIC);
            swiftsearch::put_u8(out, VERSION);
            swiftsearch::put_u8(out, static_cast<std::uint8_t>(FIELD_BYTES));
            const swiftsearch::Bytes packed(bits);
            out.insert(out.end(), packed.begin(), packed.end());
            swiftsearch::put_u32_le(out, swiftsearch::crc32c(out));
            return out;
        }

        /// Parses what encode() wrote. nullopt for anything else: wrong size, magic, version or CRC,
        /// or fields that are not a real date.
        static std::optional<StartDate> decode(const std::span<const std::uint8_t> in){
            if(in.size() != FILE_BYTES){
                return std::nullopt;
            }
            if(swiftsearch::load_u32_le(in.data()) != MAGIC || in[4] != VERSION || in[5] != FIELD_BYTES){
                return std::nullopt;
            }
            const std::size_t crc_at = FILE_BYTES - 4;
            if(swiftsearch::load_u32_le(in.data() + crc_at) != swiftsearch::crc32c(in.first(crc_at))){
                return std::nullopt;
            }

            const bstd::bit::BitBuffer bits(
                std::vector<std::uint8_t>(in.begin() + HEADER_BYTES, in.begin() + HEADER_BYTES + FIELD_BYTES));
            std::size_t at = 0;
            const auto s = bin8::read_bits(bits, at);
            const auto mi = bin8::read_bits(bits, at);
            const auto h = bin8::read_bits(bits, at);
            const auto d = bin8::read_bits(bits, at);
            const auto mo = bin8::read_bits(bits, at);
            const auto y = bin32::read_bits(bits, at);
            if(!s || !mi || !h || !d || !mo || !y){
                return std::nullopt;
            }
            const StartDate out(*s, *mi, *h, *d, *mo, *y);
            if(!out.toTimePoint().has_value()){
                return std::nullopt;
            }
            return out;
        }

        /// Writes the file atomically: a crash leaves either the old file or the new one, never half of one.
        /// @return false if it could not be written
        bool write(const fsPath& file) const{
            return writeFileAtomically(file, encode());
        }

        /// @return nullopt if the file is missing, the wrong size, or fails decode()
        static std::optional<StartDate> read(const fsPath& file){
            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if(ec || size != FILE_BYTES){
                return std::nullopt;
            }
            std::ifstream in(file, std::ios::binary);
            if(!in){
                return std::nullopt;
            }
            const swiftsearch::Bytes buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return decode(buf);
        }
    };

    //opens the start date in `folder`, creating the file on the very first run. `created` says which happened.
    //A file that exists but is damaged is an error, not a reason to start over: that would shift every timestamp.
    static StartDate openStartDate(const fsPath& folder, bool& created){
        const fsPath file = folder / META_FILE;
        if(std::filesystem::exists(file)){
            const auto existing = StartDate::read(file);
            if(!existing.has_value()){
                throw std::runtime_error(file.string() + " is damaged");
            }
            created = false;
            return *existing;
        }
        const StartDate fresh = StartDate::now();
        if(!fresh.write(file)){
            throw std::runtime_error("could not write " + file.string());
        }
        created = true;
        return fresh;
    }

    private:
    fsPath _store_path;
    std::uint64_t _segment_limit;
    std::size_t _cache_budget;

    /** @brief SEGMENTS ---------------------------------------------- */
    std::map<std::uint32_t, std::shared_ptr<LockableFile>> _segments; //newest = highest id
    mutable std::shared_mutex _segments_mtx;

    /** @brief LOOKUP ---------------------------------------------- */

    std::mutex _write_mtx; //one writer at a time: append + table update are one step
    swiftsearch::LookupTable _lookup;

    /** @brief CACHE ---------------------------------------------- */
    //which keys are cached in RAM, oldest first. Evicted from the front once over budget
    std::mutex _cache_mtx;
    std::deque<std::pair<std::string, std::size_t>> _cache_fifo;
    std::size_t _cache_bytes = 0;

    /** @brief POOL ---------------------------------------------- */
    mutable std::shared_mutex _pool_mtx; //shared while submitting, exclusive while scaleDownCpu swaps the pool
    std::unique_ptr<swiftsearch::AdaptiveThreadPool> _pool;



    /** @brief MSC ---------------------------------------------------- */
    std::vector<std::string> _warnings; //what replay had to cut off or skip
    bool _start_created = false; //true when this open created store.meta rather than reading it
    StartDate _secs_since_start; //when the database was first initialised; records store seconds after this
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

    void replaySegment(LockableFile& seg, const bool is_newest){
        using swiftsearch::DecodeStatus;
        const std::uint64_t end = seg.size();
        std::uint64_t at = 0;

        while(at < end){
            const std::uint64_t remaining = end - at;
            const auto head = seg.readAt(at, std::min<std::uint64_t>(swiftsearch::HEADER_SIZE, remaining));
            if(!head.has_value()){
                throw std::runtime_error("read failed in segment " + std::to_string(seg.id()));
            }

            const auto total = recordSize(*head);
            if(!total.has_value()){
                if(swiftsearch::isPolicy(total.error().status)){
                    //intact bytes this build will not interpret: refuse to start rather than cut data away
                    throw std::runtime_error("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": " + std::string(swiftsearch::describe(total.error().status)));
                }
                endReplayAt(seg, at, total.error().status, is_newest); //torn, not a record, or header rot
                return;
            }
            if(*total > remaining){
                endReplayAt(seg, at, DecodeStatus::Truncated, is_newest); //header fine, body never finished
                return;
            }

            const auto whole = seg.readAt(at, *total);
            if(!whole.has_value()){
                throw std::runtime_error("read failed in segment " + std::to_string(seg.id()));
            }
            const auto decoded = swiftsearch::decodeRecord(*whole);
            if(!decoded.has_value()){
                const DecodeStatus status = decoded.error().status;
                if(swiftsearch::isPolicy(status)){
                    throw std::runtime_error("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": " + std::string(swiftsearch::describe(status)));
                }
                if(status == DecodeStatus::BodyCrcMismatch && !(is_newest && at + *total == end)){
                    //the header was sound so the length is too: skip this one record and keep the rest.
                    //The key is untrusted (only the body CRC covered it), so it is not named here.
                    _warnings.push_back("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                        + ": record body is damaged, skipped");
                    at += *total;
                    continue;
                }
                endReplayAt(seg, at, status, is_newest); //includes a bad record that is the very last write
                return;
            }

            const auto& envelope = decoded->envelope;
            if(envelope.isTombstone()){
                _lookup.erase(envelope.key());
            }else if(!_lookup.putDisk(envelope.key(), swiftsearch::DiskLocation{seg.id(), at, *total})){
                _warnings.push_back("segment " + std::to_string(seg.id()) + " offset " + std::to_string(at)
                    + ": record with an empty key, skipped");
            }
            at += *total;
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

    Error compact(){
       return std::nullopt;

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
    Error putInLookupTable(const std::string& name, const swiftsearch::DiskLocation& where){
        try{
            if(!_lookup.putDisk(name, where)){
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


    //note a key was just cached and push the oldest cached keys out once over budget
    void rememberCached(const std::string& key, const std::size_t bytes){
        std::vector<std::string> victims;
        {
            std::lock_guard<std::mutex> lock(_cache_mtx);
            _cache_fifo.emplace_back(key, bytes);
            _cache_bytes += bytes;
            while(_cache_bytes > _cache_budget && _cache_fifo.size() > 1){
                _cache_bytes -= _cache_fifo.front().second;
                victims.push_back(std::move(_cache_fifo.front().first));
                _cache_fifo.pop_front();
            }
        }
        for(const auto& victim : victims){
            _lookup.evict(victim);
        }
    }


    std::optional<swiftsearch::Bytes> thread_gets(const std::string& item){
        const auto entry = location_in_lookup(item);
        if(!entry.has_value()){
            return std::nullopt;
        }
        if(entry->inMemory()){
            return *entry->memory; //copy out; the shared buffer stays in the table
        }
        if(!entry->onDisk()){
            return std::nullopt;
        }

        //on disk only. a miss here means the key is known but the read failed
        auto bytes = find_and_read(item, *entry->disk);
        if(bytes.has_value()){
            cacheLoaded(item, *entry->disk, *bytes);
        }
        return bytes;
    }


    //puts a value just read from `where` into the in-memory cache and counts it against the budget.
    //@return false if it did not go in: too big for the budget, already cached, or the key was
    //overwritten or relocated while it was being read (all harmless, the caller still has the bytes)
    bool cacheLoaded(const std::string& key, const swiftsearch::DiskLocation& where, const swiftsearch::Bytes& bytes){
        if(bytes.size() > _cache_budget){
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
        const std::uint64_t fixed = swiftsearch::RECORD_OVERHEAD + key.size();
        const std::uint64_t payload = entry->disk->length > fixed ? entry->disk->length - fixed : 0;
        if(payload > _cache_budget){
            return BAD("value is larger than the memory budget");
        }

        const auto bytes = find_and_read(key, *entry->disk);
        if(!bytes.has_value()){
            return BAD("could not read the value from disk");
        }
        if(cacheLoaded(key, *entry->disk, *bytes)){
            return GOOD;
        }
        //cacheLoaded refuses when another reader cached it first, which is fine, or when the key
        //changed under us, which means the value we loaded is no longer the live one
        const auto now = location_in_lookup(key);
        if(now.has_value() && now->inMemory()){
            return GOOD;
        }
        return BAD("the key changed while it was being loaded, try again");
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
        std::future<std::optional<swiftsearch::Bytes>> answer;
        {
            std::shared_lock<std::shared_mutex> lock(_pool_mtx);
            answer = _pool->submit([this, key = std::string(item)]{ return thread_gets(key); });
        }
        return answer.get();
    }

    /// Durable when this returns GOOD: the record is on disk and fsync'd.
    Error Put(const std::string& name, const swiftsearch::Bytes& _data,
              const VAR_TYPE type = VAR_TYPE::BIN){
        if(!swiftsearch::keys::is_valid(name)){
            return BAD("invalid key");
        }
        try{
            const auto secs = secondsSinceStart();
            if(!secs.has_value()){
                return BAD("the store is more than 136 years past its start date");
            }
            const auto record = swiftsearch::Envelope::value(name, _data, *secs, type).encode();

            std::lock_guard<std::mutex> lock(_write_mtx);
            const auto where = appendLocked(record);
            if(!where.has_value()){
                return BAD(where.error());
            }
            return putInLookupTable(name, *where);
        }catch(const std::length_error&){
            return BAD("key or value is over the size limit");
        }catch(const std::exception& e){
            return BAD(std::string("file failed to store: ") + e.what());
        }
    }

    /// Deleting a key that does not exist is not an error. Durable when this returns GOOD.
    Error Delete(const std::string_view& item){
        const std::string key(item);
        try{
            std::lock_guard<std::mutex> lock(_write_mtx);
            if(!_lookup.contains(key)){
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
     * ramps up again below `low`. Takes effect by replacing the pool, which finishes
     * the reads already queued first.
     */
    Error scaleDownCpu(const std::size_t& hi, const std::size_t& low){
        if(hi <= low || hi > 100){
            return BAD("values for scaling are out of range");
        }
        try{
            swiftsearch::ThreadPoolOptions options;
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


    /// Number of live keys.
    std::size_t Size() const{
        return _lookup.size();
    }

    /// What startup replay cut off or skipped. Empty after a clean shutdown.
    const std::vector<std::string>& Warnings() const noexcept{
        return _warnings;
    }

};
