#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>
#include <atomic>
#include "bstd/bit/bitBuffer.hpp"
#pragma once


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
template <bool NoCapitals>
class CharacterBucket {
    std::vector<atomCharPair> _buckets;

    static constexpr char transform(char c) noexcept {
        if constexpr (NoCapitals)
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        else
            return c;
    }
    
    public:
        void addTo(char v) {
            _buckets[static_cast<unsigned char>(transform(v))].increment();
        }
        
        std::size_t countAt(const char& c){
            return _buckets.at(c).getCount();
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
    char makeIntoChar(std::size_t startIndex) const noexcept{
        std::bitset<8> c{};
        int ind = 0;
        for(std::size_t i = startIndex; i < (startIndex + 8); i++){
            c.set(ind, this->at(i));
            ind++;
        }
        return static_cast<char>(c.to_ulong());
    }
};



class Envelope{
    /*
    [how long?][name][null term][underlying data]
    */

    using BitBuffer = bstd::bit::BitBuffer;
    static constexpr char END_OF_TEXT = '\0';
    friend class EnvelopeUnseal;
    private:
    //how many bytes will be needed to store size variable?
    static constexpr std::uint8_t how_many_bytes(std::uint64_t n) {
        if (n <= 0xFFull)return 1;
        if (n <= 0xFFFFull) return 2;
        if (n <= 0xFFFFFFull) return 3;
        if (n <= 0xFFFFFFFFull) return 4;
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
    explicit Envelope(const std::string& name, const ImprovedBuffer& bitBuf){
        _data = init(name, bitBuf);
    }

    std::size_t totalSize(){
        return _data.size();   
    }

    ImprovedBuffer underlying_data(){
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
        ImprovedBuffer temp{};
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
    
    std::pair<std::string, ImprovedBuffer> contents(){
        return {name(), underlying_data()};
    }

    

    private:
    //first 15 bytes : chars of name, next 4.5: size of incoming data: 
    ImprovedBuffer _data;
    std::size_t _jump_to_null_terminator{0};
};


class Box{
    static constexpr std::array<bool, 5> _BOX_PREFIX{true, false, true, false, true};
    using box_or_envelope = std::variant<Box, Envelope>;
    friend class Envelope;
    private:
    std::string _box_name;
    std::vector<box_or_envelope> _contents;
    public:

    /**
    * add a box with no contents inside
    */
    Box(const std::string& name_of_box) : _box_name(name_of_box){}

    Box(const std::string& name, const std::vector<box_or_envelope>& boxes) : _box_name(name), _contents(boxes){};

    void addToBox(std::string& name, const ImprovedBuffer& buf){
        Envelope temp{name, buf};
        _contents.push_back(temp);
    }
    void addToBox(const ImprovedBuffer& pre_serialized){

    }

    std::optional<ImprovedBuffer> pullOut(const std::string& name){
        return std::nullopt;
    }

    std::optional<ImprovedBuffer> deepSearch(const std::string& name){
        return std::nullopt;
    }

    





};




class EnvelopeUnseal{
    private:
    std::optional<int> expect_number_for(const Envelope& envelope){
        int i = 0;
        while(envelope._data.at(i)){
            i++;
            if(i > 8){
                return std::nullopt;
            }
        }
        return i;
    }


    u_int64_t total_expected_bytes(const int& expected, const ImprovedBuffer& buf, const std::size_t& cursor){
        u_int64_t accumulate = 0;
        u_int64_t byte_is_worth = 1;
        const std::size_t end = cursor + (expected * 8);
        for(std::size_t i{end}; cursor < i; i--){
            if(buf.at(i)){
                accumulate += byte_is_worth;
            }
            byte_is_worth *= 2;
        }
        return accumulate;
    }

    /*
    [how long?][name][null term][underlying data]
    */
    public:
    std::optional<std::pair<std::string, std::string>> getAsString(const Envelope& envelope){
        std::size_t cursor = 0;
        auto expected_bytes_wrap = expect_number_for(envelope);
        if(!expected_bytes_wrap.has_value()){
            return std::nullopt;
        }
        
        auto expected_bytes = expected_bytes_wrap.value();
        cursor = expected_bytes;

        const u_int64_t total_bytes = total_expected_bytes(expected_bytes, envelope._data, cursor);

        char current = '0';
        std::string name{};
        while(current == '\0' && cursor < total_bytes){
            current = envelope._data.makeIntoChar(cursor);
            name.push_back(current);
            cursor += 8;
        }
        cursor += 8; //skip over '\0'
        std::string data = envelope._data.subBuffer(cursor, envelope._data.end().idx).toString();
        std::pair<std::string, std::string> t{name, data};
        return t;
    }

};



