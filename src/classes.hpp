#include <vector>
#include <atomic>
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