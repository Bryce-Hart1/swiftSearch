#pragma once

#include <string>
#include <mutex>
#include <filesystem>
#include <memory>
#include <queue>



class fileTreeNode{
    std::string path;
    std::vector<std::unique_ptr<fileTreeNode>> children;
    bool isChild;
};

class fileTree{
    friend class fileTreeNode; 


    private:
        fileTreeNode root;
        size_t size;

    public:
        fileTree(){};
        fileTree(std::string root){};
        std::queue<std::string> queueToVisit();
        bool isChildNode();






};

std::string toLowerCase(std::string str);

int tokenOne(std::string token);
int tokenTwo(std::string token);


void tokenMain(int count, char* argArr[]);


