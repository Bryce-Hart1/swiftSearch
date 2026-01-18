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
char dontConvertToLowerCase(unsigned char value);

class atomNode {
    public:
        atomNode();
        atomNode(bool isRoot, char value);
        
        int getCounter(std::string findNode) const;
        void incrementCounter(std::string atValue);
        
    private:
        bool isRoot;
        char value;
        std::atomic<int> counter;
        std::vector<std::unique_ptr<atomNode>> childrenNodes;
        std::mutex nodeMutex;

        friend class atomTrie; //shared private members with ouside
};



class atomTrie {
    using ptrToType = char(*)(unsigned char); //to call function

public:



    ptrToType callFunction;


    atomTrie();
    atomTrie(bool isCaseSensitive);
    
    void insert(const std::string& word);
    bool doesExist(const std::string& word) const ;
    int getDepth() const ;



private:
    std::atomic<int> depth;
    std::unique_ptr<atomNode> root;
    bool caseSensitive;
};




class characterBucket {
public:
    static constexpr int MAXSIZE = 255;
    
    characterBucket();
    ~characterBucket();
    
    void push(char value);
    unsigned int getCount(char value) const;
    void printPair(char value, int atInstance) const;

private:
struct atom_cPair {
    char value = '\0'; //using as default value
    std::unique_ptr<std::atomic<unsigned int>> count = 
        std::make_unique<std::atomic<unsigned int>>(0);
};
    
    std::array<atom_cPair, MAXSIZE> list;
};


class numberList{
        //keeps track of the type set by the user
        enum numberTypeEnum{
        TYPE_NOT_SET,
        TYPE_DOUBLE,
        TYPE_INT};

    
    private:
    bool isIntegerList;
    bool needsRounded;

    
    std::vector<long double> mainListDouble;
    std::vector<long long int> mainListInt;
    std::atomic<long long int> sizeOfList;



    public:
    numberTypeEnum type = TYPE_NOT_SET;
    numberList();
    numberList(bool isIntegerList);
    unsigned int getSize();
    bool listNeedsRounded();
    long long int roundList(bool roundUp);
    void sortList();
    void grabFromThreads(); 
};


class simpleCount{
    public:
    void incrementBy(int amount);
    unsigned int getCount();
    private:
    std::atomic<unsigned int> count;
};






class Timer{
    public:
        void start();
        void stop();
        double timeInMilliseconds() const;
        double timeInSeconds() const ;
        void reset();
        void printCurrentTime() const ;

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
    file(std::string name, bool assignedAsRoot);
    std::string returnFileName();
    long double returnFileSize();
    private:
    std::string fileName;
    std::uintmax_t sizeOfFile;
    bool isRoot;
    bool isDirectory;
    

};
class fileTreeStructure{
    public:
    fileTreeStructure(file root);
    std::string getNameOfNext();
    private:
    std::queue<file> orderOfFilesToOpen;

    
};
