#pragma once

#include <string>
#include <exception>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>



class atomTrie {
public:

    class node {
    public:
        node();
        
        int getCounter() const;
        void incrementCounter();
        
    private:
        char value;
        std::atomic<int> counter;
        std::vector<std::unique_ptr<node>> childrenNodes;
        std::mutex nodeMutex;

        friend class atomTrie; //shared private members with ouside
    };

    atomTrie();
    
    void insert(const std::string& word);
    bool doesExist(const std::string& word) const;
    int getDepth() const;

private:
    std::atomic<int> depth;
    std::unique_ptr<node> root;
    bool caseSensitive;
    unsigned char toLower();
};




class characterBucket {
public:
    static constexpr int MAXSIZE = 255;
    
    characterBucket();
    
    void push(char value);
    unsigned int getCount(char value) const;
    void printPair(char value, int atInstance) const;

private:
    struct cPair {
        char value = '\0';
        unsigned int count = 0;
    };
    
    std::array<cPair, MAXSIZE> list;
};



class numberList{
    private:
    bool isInteger;
    std::vector<long double> mainList;


    public:
    numberList();
    numberList(bool isInteger);

};