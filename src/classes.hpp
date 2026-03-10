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
#include <deque>


using highResClock = std::chrono::time_point<std::chrono::high_resolution_clock>;



char toLower(unsigned char value);


class atomCharPair{
    public:
    void increment();
    char getValue();
    int getCount();
    void setIntialValue();
    private:
    char value;
    std::atomic<size_t> count;
};


class characterBucket {
    
public:
    characterBucket();
    void addTo(char value);
    void printAll();
    char getValueAt(int value);
    size_t getCounterAt(int value);


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
    highResClock startTime;
    highResClock endTime;
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
    std::filesystem::path getfsPath(); //this should be primary condition of each file
    std::string getFileName();
    unsigned long long int getFileSize();
    std::string returnReadableSize();
    std::filesystem::file_type getEnumType();
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
public:
    numberList();
    std::vector<long double> sendDoubleVector();
    std::vector<long long int> sendIntVector();
    void add(long long int data);
    void add(long double data);
    void addVec(numberList data);
    void setType(LIST_TYPE);
    bool isIntTypeList();
    void printList();
    void printToFile(std::string preDefName);
    void sort();
    void reverse();
    private:
    LIST_TYPE type;
    std::vector<long double> dList;
    std::vector<long long int> iList;
};


namespace numberListHelper{
    numberList combinedList(std::queue<file> files);
    inline std::mutex combinedListLock;
}


class parseWindow{
    public:
    parseWindow(size_t size);
    void move(char c);
    std::string getWindow() const;
    bool isCorrectSequence(const std::string& compare) const;
    private:
    std::deque<char> window;
    size_t windowSize;
};