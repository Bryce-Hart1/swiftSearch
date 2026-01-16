#pragma once

#include <string> 
#include <mutex>
#include <filesystem>
#include <memory>
#include<vector>

class fileTreeNode{
    private:
    std::string path;
    std::vector<std::unique_ptr<fileTreeNode>> children;
    bool isChild;
    public:
    std::string getPath();
    fileTreeNode();
    fileTreeNode(const std::string& name);
};


//this will create a class based of the fileTreeNode given. 

class fileTree{
    friend class fileTreeNode; 


    private:
        fileTreeNode root;
        size_t size;

    public:
        fileTree();
        fileTree(std::string root);
        fileTreeNode getDirectoryRoot();
        std::queue<std::string> queueToVisit(std::string root);
        bool isChildNode(std::string path);






};

std::string toLowerCase(std::string str);

std::string makeSpellCheck(std::string input, std::vector<std::string> expected);

int checkTokenValid(int place, std::string tokenExtracted);


void tokenMain(std::string arg);


