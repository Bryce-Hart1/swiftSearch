#include "classes.hpp"
#include "constants.hpp"


#include <vector>
#include <array>
#include <exception>
#include <string>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <memory>
#include <sstream>
#include <functional>
#include <fstream>






char toLower(unsigned char value) {
    if(value >= 'A' && value <= 'Z') {
        return value + 32;
    }
    return value;
}

char dontConvertToLowerCase(unsigned char value) {
    return value;
}

/**
 * @class node class belongs to atomTrie, while 
 * 
 * 
 */
atomNode::atomNode(){
    if(DEBUG_ACTIVE){
        std::cout << "atomTrie::node is being called with a default constructor." << std::endl;
    }
}
atomNode::atomNode(bool isRoot, char value){

}


int atomNode::getCounter(std::string findNode) const{

    return 0;
}

void atomNode::incrementCounter(std::string atValue) {


}





atomTrie::atomTrie(){
    if(DEBUG_ACTIVE){
        std::cout << "atomTrie constuctor called default constructor" << std::endl;
    }
}

atomTrie::atomTrie(bool isCaseSensitive) {
    if(isCaseSensitive) {
        callFunction = dontConvertToLowerCase;
    } else {
        callFunction = toLower;//if not convert to lower
    }
}

void atomTrie::insert(const std::string &word) {
    for(char c : word) {
        char converted = callFunction(c); //call function 
    }
}

bool atomTrie::doesExist(const std::string& word ) const {
    return false;
}


int atomTrie::getDepth() const {
    return depth;
}







/**
 * @class characterBucket - stores counts of all characters found
 * @details does a O(1) opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * 
 */
struct atom_cPair {
    char value = '\0'; //using as default value
    std::unique_ptr<std::atomic<unsigned int>> count = 
        std::make_unique<std::atomic<unsigned int>>(0);
};
    
    characterBucket::characterBucket() {
    for (auto& pair : list) {
        pair.count = 0; //set intial to 0
    }
}

characterBucket::~characterBucket() {

};



//push a new count to the currentlist
void characterBucket::push(char value){
//atomically set up
}
//return value at a character value
unsigned int characterBucket::getCount(char value) const{
    return 0; 
}

void characterBucket::printPair(char value, int atInstance) const {
    //printf("at instance: [%d], value is %d", value, list.at(atInstance)); there is a reason this is commented out btw..
}





numberList::numberList(){
    isIntegerList = false; //unless told otherwise
};

numberList::numberList(bool i) : isIntegerList(i) {};


bool numberList::listNeedsRounded(){
    if(this->needsRounded){
        return true;
    }
    return false;
}


void numberList::sortList(){
    if(TYPE_NOT_SET && DEBUG_ACTIVE){
        std::cout << "type is Not set in numberList::sortlist" << std::endl;
    }
    if(TYPE_DOUBLE){
        
    }
    if(TYPE_INT){

    }
    
}


long long int numberList::roundList(bool roundUp){
    if(!isIntegerList){
    


    }
}
unsigned int numberList::getSize(){};





/**
 * @brief combines from threads
 * @details this is a better way then using atomic, as they are seperate objects
 * and would need to be transfered anyway.
 */
void numberList::grabFromThreads(){

}


unsigned int simpleCount::getCount(){
    return (this->count);
}

void simpleCount::incrementBy(int amount){
    this->count.fetch_add(amount);
}



/**
 * @class timer - this class keeps up with the clock to document how long an opertation takes
 */


void Timer::start(){
    startTime = std::chrono::high_resolution_clock::now();
    running = true;
}
    
void Timer::stop() {
    endTime = std::chrono::high_resolution_clock::now();
    running = false;
}
    
double Timer::timeInMilliseconds() const {
    auto end = running ? std::chrono::high_resolution_clock::now() : endTime;
    return std::chrono::duration<double, std::milli>(end - startTime).count();
}
    
double Timer::timeInSeconds() const {
    return timeInMilliseconds() / 1000.0;
}
    
    void Timer::reset() {
        startTime = std::chrono::high_resolution_clock::now();
        running = true;
    }
    
    void Timer::printCurrentTime() const {
        return;
    }
    
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> endTime;
    bool running = false;





file::file(std::string name, bool assignedAsRoot){
    
    this->fileName = name;
    this->isRoot = assignedAsRoot;
    try{
        this->sizeOfFile = std::filesystem::file_size(name);
        this->isDirectory = std::filesystem::is_directory(name);
        

    } catch(std::filesystem::filesystem_error& exception){
        if(DEBUG_ACTIVE){
            std::cout << "DEBUG ~~ error opening " << name << " in file constructor" << std::endl;
        }
        std::cout << "Error: " << exception.what() << std::endl;
    }

}


std::string file::returnFileName(){
    return (this->fileName);
}

//returns size in bytes
long double file::returnFileSize(){
    return static_cast<long double>(this->sizeOfFile);
}


fileTreeStructure::fileTreeStructure(file root){
    
}

std::string fileTreeStructure::getNameOfNext(){

}

