#pragma once

#include <string>
#include <exception>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>


bool debugModeIsOn;

char toLower(unsigned char value);
char dontConvertToLowerCase(unsigned char value);


class atomTrie {
public:

    class node {
    public:
        node();
        
        int getCounter(std::string findNode) const;
        void incrementCounter(std::string atValue);
        
    private:
        bool isRoot;
        char value;
        std::atomic<int> counter;
        std::vector<std::unique_ptr<node>> childrenNodes;
        std::mutex nodeMutex;

        friend class atomTrie; //shared private members with ouside
    };


    using ptrToType = char(*)(unsigned char);
    ptrToType callFunction;
    atomTrie(bool isCaseSensitive);
    
    void insert(const std::string& word);
    bool doesExist(const std::string& word) const ;
    int getDepth() const ;



private:
    std::atomic<int> depth;
    std::unique_ptr<node> root;
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