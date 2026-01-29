#pragma once

#include <string>
#include <exception>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>
#include <queue>
#include <filesystem>


char toLower(unsigned char value);


/**
 * @class designed to work with both int and char for now, will help build trees to lock 
 * 
 * 
 * designed to name the object made root and call setup on that node
 */
class atomicNode {
    public:
        atomicNode();
        atomicNode(char value);
        void increment();
        unsigned int getCount();
        char getValue();
        void setEndPointTrue();
        bool getIsEndPoint();
        atomicNode* findChildNode(char value);
        unsigned int getChildCount() const;
        void add(std::string word);
        void printSet(std::string prefix);
    private:
        char value;
        unsigned int count;
        mutable std::mutex mtx;
        bool isEndPoint;
        bool isRoot;
        std::vector<std::unique_ptr<atomicNode>> children;

};


/**
 * @class pairs with character bucket
 * increment - increments atomic count
 */
class atomCharPair{
    public:
    void increment();
    char getValue();
    int getCount();
    private:
    char value;
    std::atomic<unsigned int> count;
};


class characterBucket {
    
public:
    characterBucket(bool ignore);
    void addTo(char value);
    void printAll();
    char getValueAt(int value);
    unsigned int getCounterAt(int value);


private:
    std::array<atomCharPair, 255> buckets; //255 is for all ascii values
    bool ignoreCaps;
};


class simpleCount{
    public:
    void incrementBy(int amount);
    unsigned long long int getCount();
    private:
    std::atomic<unsigned long long int> count;
};






class Timer{
    public:
        void start();
        void stop();
        double timeInMilliseconds() const;
        void reset();
        void printCurrentTime() const ;
        std::string readableTime();
    private:
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> endTime;
    bool running = false;
};



/**
 * @class the redesigned file class, to accompany the fileTreeStructure class. This will keep basic 
 * info about the file for quick lookup
 * 
 */
class file{
    public:
    file();
    file(std::string name); //for assigning new files
    std::string getFileName();
    unsigned long long int getFileSize();
    std::string returnReadableSize();
    std::filesystem::file_type getEnumType();
    std::filesystem::path getfsPath();
    std::string filePathToStr();
    void setFileName(std::string name);
    void setFileSize(std::uintmax_t size);
    void setEnumType(std::filesystem::file_type type);
    void setPath(std::filesystem::path path);
    private:
    std::string fileName;
    std::uintmax_t sizeOfFile;
    std::filesystem::file_type enumFileType;
    std::filesystem::path fsPath; //Non string type
};


class fileTreeStructure{
    public:
    fileTreeStructure(file root); //putting setup in constructor

    std::queue<file> createFileQueue();
    std::string getNameAt(int value); //meant to interate over
    std::string fileTypeToString(std::filesystem::file_type type);
    std::filesystem::file_type returnEnumTypeAt(int value);
    std::string getPathAt(int value);
    size_t entrysInStruct();
    void printAll();
    private:
    std::vector<file> storedFiles;
    
};

class numberList{
    enum class LIST_TYPE{
        UNSET,
        DOUBLE,
        INT
    };
    numberList();
    std::vector<long double> sendDoubleVector();
    std::vector<long long int> sendIntVector();
    void add(auto data);
    void setType(LIST_TYPE);
    bool isIntTypeList();
    void printList();
    private:
    LIST_TYPE type;
    std::vector<long double> dList;
    std::vector<long long int> iList;
};


namespace numberListHelper{
    numberList combinedList(std::queue<file> files);
    inline std::mutex combinedListLock;
    numberList singleList(std::string file);
}