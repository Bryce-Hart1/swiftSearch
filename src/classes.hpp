#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>
#include <atomic>
#include "bstd/bit/bitBuffer.hpp"
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
    ImprovedBuffer subBuffer(const std::size_t& begin, const std::size_t& end){
        if(begin > end || end > size() || begin < 0){
            std::out_of_range("requested buffer parameters are out of range");
        }
        ImprovedBuffer t{};
        for(std::size_t i{0}; i < end; i++){
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
    u_int8_t how_many_bytes(const std::size_t& size_in_bytes){
        if(size_in_bytes < 255){
            return 1;
        }else if(size_in_bytes < )
    }
    data init(const std::string& name, const ImprovedBuffer& bitBuf){
        ImprovedBuffer initialize{};
        const std::size_t full_size = (name.size() + 1) + bitBuf.byteSize();
        for(const char& c : name){
            initialize.push(static_cast<uint8_t>(c));
        }
        initialize.push(static_cast<u_int8_t>(END_OF_TEXT)); //end name

        
    }
    public:
    explicit data(const std::string& name, const ImprovedBuffer& bitBuf){
        _data = init(name, bitBuf);
    }

    std::size_t totalSize(){
        return _data.size();   
    }

    BitBuffer underlying_data(){
        return _data;
    }

    private:
    //first 15 bytes : chars of name, next 4.5: size of incoming data: 
    ImprovedBuffer _data;
};



