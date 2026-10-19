#include "bstd/bit/bitBuffer.hpp"
#include "bstd/bit/writeBin.hpp"
#include "bstd/data_structure/threadsafe/tsTrie.hpp"
#include "bstd/data_structure/threadsafe/tsVector.hpp"
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

    template<typename T>
    struct data {
    public:
        data(std::array<char, 15> name) : _name(std::move(name)){
            
        }

        





    private:
        std::size_t _size;
        std::array<char, 15> _name;
        bstd::bit::BitBuffer _data; //includes 
    };

    struct lookupItem{

    }

    template<typename T>
    using tsVec = bstd::ds::ts::vec<T>;
    using fsPath = std::filesystem::path;


    private:
    std::vector<LockableFile> _files;
    bstd::ds::ts::Trie _table_lookup;
    fsPath _store_path;

    fsPath createDatabaseFolder(const fsPath& destination){
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


    public:
    SwiftSearch(const fsPath& path) : _store_path(path){
        try {
            std::filesystem::path dbDir = createDatabaseFolder("swiftSearch/db");
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << " [" << e.path1() << "]\n";
        }
        catch (const std::invalid_argument& e) {
            std::cerr << e.what() << '\n';
        }
    }



    







};