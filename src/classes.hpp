#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>
#include <atomic>
#include "bstd/bit/bitBuffer.hpp"
#include "bstd/store/serialize.hpp"
#pragma once

namespace help{
inline char toLower(unsigned char value) {
    if(value >= 'A' && value <= 'Z') {
        return value + 32;
    }
    return value;
}


} //namespace help
/**
 * @class partners with characterBucket
 * holds a set of values 
 * [a]
 * [1]
 */
class atomCharPair{
    private:
    char _value;
    std::atomic<std::size_t> _count;

    public:
    atomCharPair(const char& value) : _value(value), _count(0){}

    void increment(){
        this->_count.fetch_add(1);
    }

    std::size_t getCount() const{
        return (this->_count.load(std::memory_order_relaxed));
    }
    char value() const noexcept{
        return this->_value;
    }
};

/**
 * @class characterBucket - stores counts of all characters found
 * @details does a opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * holds in array style, where everytime an element is accessed by the value of its char
 * [a][b][c][d][e]...
 * [1][0][9][3][5]...
 * MycharacterBucket.addTo(b);
 * [a][b][c][d][e]...
 * [1][1][9][3][5]...
 */
class CharacterBucket{
    std::vector<atomCharPair> _buckets;
    bool _ignoreCaps;
public:
    explicit CharacterBucket(bool no_capitals) : _ignoreCaps(no_capitals) {}
  /**
    * char is signed on this platform, so any byte >= 0x80 (every UTF-8 continuation
    * byte, every accented letter, smart quote, em dash) used to convert to a huge
    * size_type and make .at() throw. charFreq caught that and abandoned the rest of
    * the file, so non-ASCII input silently produced wrong counts.
    */
    void addTo(char value){
        this->_buckets[static_cast<unsigned char>(value)].increment();
    }
    std::size_t totalSize() const noexcept{
        std::size_t r{0};
        for(const auto& bucket : _buckets){
            r += bucket.getCount();
        }
        return r;
    }
};

/**
 * @class an atomic counter.
 * use getCount to get count from a value, and incrementBy to change the count by a given amount.
 */
class SimpleCount{
    private:
    std::atomic<std::size_t> count;


    std::size_t getCount() const{
        return (this->count.load(std::memory_order_relaxed));
    }

    void incrementBy(int amount){
        this->count.fetch_add(amount);
    }



};

class ImprovedBuffer : public bstd::bit::BitBuffer{
    public: 
    ImprovedBuffer subBuffer(std::size_t begin, std::size_t end) const {
        if (begin > end || end > size()) {
            throw std::out_of_range("requested buffer parameters are out of range");
        }
        ImprovedBuffer t{};
        for (std::size_t i = begin; i < end; ++i) {
            t.push(at(i));
        }
        return t;
    }
};


class data{
    using BitBuffer = bstd::bit::BitBuffer;
    static constexpr unsigned char NAME_SIZE = 15;
    static constexpr unsigned char DATA_MARKER_SIZE = 1;
    static constexpr char END_OF_TEXT = '\0';

    private:
    //how many bytes will be needed to store size variable?
    static constexpr std::uint8_t how_many_bytes(std::uint64_t n) {
        if (n <= 0xFFull)        return 1;
        if (n <= 0xFFFFull)      return 2;
        if (n <= 0xFFFFFFull)    return 3;
        if (n <= 0xFFFFFFFFull)  return 4;
        return 8;
    }

    ImprovedBuffer init(const std::string& name, const ImprovedBuffer& payload) {
        ImprovedBuffer out{};
        for(unsigned char c : name) out.push(static_cast<std::uint8_t>(c));
        out.push(static_cast<std::uint8_t>(END_OF_TEXT));
        _jump_to_null_terminator = (name.size() + 1) * 8;

        std::uint8_t n = how_many_bytes(payload.size());
        while (n--) out.push(true);
        out.push(false);

        BitBuffer number{};
        number.from(payload.size());
        out.push(number);
        out.push(payload);
        return out;
    }

    public:
    explicit data(const std::string& name, const ImprovedBuffer& bitBuf){
        _data = init(name, bitBuf);
    }

    std::size_t totalSize(){
        return _data.size();   
    }

    BitBuffer underlying_data(){
        std::size_t itr = _jump_to_null_terminator;
        int skip_past = 0;
        while(true){
            if(_data.at(itr)){
                skip_past++;
            }else{
                break;
            }
            itr++;
        }
        itr++; //go over 0
        itr += (8 * skip_past);
        return _data.subBuffer(itr, _data.end().idx);
    }

    std::string name(){
        std::string n{};
        char c = '0';
        std::size_t itr{0};
        BitBuffer temp{};
        while(c != '\0'){
            for(int i = 0; i < 8; i++){
                temp.push(_data.at(itr));
                itr++;
            }
            c = temp.toString().at(0);
            n.push_back(c);
            temp.clear();
        }
        return n;
    }

    private:
    //first 15 bytes : chars of name, next 4.5: size of incoming data: 
    ImprovedBuffer _data;
    std::size_t _jump_to_null_terminator{0};
};



