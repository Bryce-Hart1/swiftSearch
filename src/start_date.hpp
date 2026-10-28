#pragma once
/**
 * StartDate: the instant a store was first initialised, and the small binary file
 * (store.meta) that remembers it. Records store only "seconds since this instant";
 * see RecordHeader::secs_since_start in record.hpp.
 *
 * Standalone on purpose: it needs no store, only the bin types, the byte codec, the
 * CRC and the atomic file writer, so it can be built, written and read back in a test.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>
#include <variant>
#include <vector>

#include "bstd/bit/bitBuffer.hpp"
#include "bstd/store/serialize.hpp"
#include "bytes.hpp"
#include "crc32c.hpp"
#include "file_sync.hpp"

namespace swiftsearch {

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
        using sc8 = static_cast<std::uint8_t>;
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
    bool write(const std::filesystem::path& file) const{
        return writeFileAtomically(file, encode());
    }

    /// @return nullopt if the file is missing, the wrong size, or fails decode()
    static std::optional<StartDate> read(const std::filesystem::path& file){
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

}  // namespace swiftsearch
