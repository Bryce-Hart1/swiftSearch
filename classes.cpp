#include<classes.h>
#include<vector>
#include<array>
#include<exception>
#include<string>
#include<atomic>
#include<filesystem>
#include<iostream>
#include<mutex>
#include<memory>
#include<sstream>
#include<print>

class customExceptions : public std::exception{
    private : std::string message;

    public : 
        customExceptions(const std::string& msg) : message(msg) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
};



/**
 * @struct shows node in the directory tree.
 */
struct FileTreeNode{
    std::string Name;
    bool isDirectory;
    std::vector<FileTreeNode*> children;

    FileTreeNode(const std::string& name, bool isDir) : Name(name), isDirectory(isDir){}
    





};



/**
 * 
 * 
 * 
 * 
 * @details adds nodes based a lock
 */



//Node has count of times found, char* value 
atomTrie::node::node() : counter(0){}


atomTrie::atomTrie() : root(std::make_unique<node>()) {}





void atomTrie::insert(const std::string &word){

};

bool doesExist(){
    
};

unsigned char toLower(unsigned char value){
    if(value >= 'A' && value <= 'Z'){
        return (value - 32); //ascii difference between them
    }
    return value;
}

int atomTrie::getDepth() const{
    return depth;
};













/**
 * @details does a O(1) opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * 
 */
class characterBucket{

    public:
        static const int MAXSIZE = 255; //size of array based off of max ASCII


    private:
    struct cPair{
        char value;
        unsigned int count;
    };

    

    private:
    std::array<cPair, MAXSIZE> list;



    //push a new count to the currentlist
    void push(char value){
        list[value].count++;
    }
    //return value at a character value
    unsigned int getCount(char value){
        return list[value].count;
    }

    void printPair(char value, int atInstance){
        std::println("key: {}, value {}", list[atInstance].value, list[atInstance].count );
    }


};




numberList::numberList(){
    isInteger = false;
};

numberList::numberList(bool i) : isInteger(i) {};



