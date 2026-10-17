#include "bstd/bit/bitBuffer.hpp"
#include "bstd/bit/writeBin.hpp"
#include "bstd/data_structure/threadsafe/tsTrie.hpp"
#include "bstd/data_structure/threadsafe/tsVector.hpp"
#include "tokenize.cpp"
#include "bstd/bstd.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <variant>




class SwiftSearch{
    private:
    class LockableFile{
        
        private:
        
        std::shared_mutex _mtx;
        std::fstream _file;
        std::string _name_of_write_space;
        public:
        //assumes file has been opened
        void write(const u_int8_t& b){
            std::unique_lock<std::shared_mutex> lock(this->_mtx);
            bstd::bit::writeBinary(_file, b);
        }
        //assumes file has been opened
        std::optional<bstd::bit::BitBuffer> read(std::istream &in, const std::size_t amount){
            std::unique_lock<std::shared_mutex> lock(this->_mtx);
            return bstd::bit::readBinary(in, amount);
        }
        LockableFile(const std::string& name) : _name_of_write_space(name){

        };


    };
    using data = std::variant<>
    

    public:
    






    private:
    std::vector<LockableFile> _files;
    bstd::ds::ts::Trie _lookup;

};