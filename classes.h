#pragma once

#include <string>
#include <exception>
#include <array>
#include <atomic>
#include<vector>



struct FileTreeNode {
    std::string name = "";
    bool isDirectory = false;
    int depth = 0;
    FileTreeNode* parent = nullptr;
    
    FileTreeNode() = default;
    FileTreeNode(const std::string& n, bool isDir);
};

class atomTrie {
public:

    class node {
    public:
        node();
        node(char val);
        
        char getValue() const;
        int getCounter() const;
        void incrementCounter();
        
    private:
        char value;
        std::atomic<int> counter;
        std::vector<node*> childrenNodes;
    };

    atomTrie();
    atomTrie(bool isCaseSensitive);
    
    void insert(const std::string& word);
    bool doesExist(const std::string& word) const;
    int getDepth() const;

private:
    std::atomic<int> depth;
    node *root;
    bool caseSensitive;
};

class characterBucket {
public:
    static constexpr int MAXSIZE = 255;
    
    characterBucket();
    
    void push(char value);
    unsigned int getCount(char value) const;
    void printPair(char value) const;

private:
    struct cPair {
        char value = '\0';
        unsigned int count = 0;
    };
    
    std::array<cPair, MAXSIZE> list;
};