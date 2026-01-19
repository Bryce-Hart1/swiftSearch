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

//default constructor gets called on root
atomicNode::atomicNode(){
    this->value = '*';
    this->root = true;
}

atomicNode::atomicNode(char value){
    this->value = value;
    this->root = false;
}

void atomicNode::increment() {
    std::lock_guard<std::mutex> lock(mtx);
    ++count;
}

unsigned int atomicNode::getCount(){
    std::lock_guard<std::mutex> lock(mtx);
    return this->count;
}

char atomicNode::getValue(){
    std::lock_guard<std::mutex> lock(mtx);
    return this->value;
}

void atomicNode::setEndPointTrue(){
    std::lock_guard<std::mutex> lock(mtx);
    this->isEndPoint = true;
}

bool atomicNode::getEndPoint(){
    return this->isEndPoint;
}

void atomicNode::addChild(){
    std::lock_guard<std::mutex> lock(mtx);
    //push back new children
}

atomicNode* atomicNode::findChild(char value){
    std::lock_guard<std::mutex> lock(mtx);
    for(auto& child : children){
        if(child->value == value){
            return child.get();
        }
    }
    return nullptr;
}

unsigned int atomicNode::getChildCount() const{
    std::lock_guard<std::mutex> lock(mtx);
    return children.size();

}










/**
 * @class partners with characterBucket
 */
void atomCharPair::increment(){
    this->count.fetch_add(1);
}




/**
 * @class characterBucket - stores counts of all characters found
 * @details does a O(1) opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * 
 */
characterBucket::characterBucket(bool ignore){
    this->ignoreCaps = ignore;
}


void characterBucket::addTo(char value){
    this->buckets.at(value).increment(); //takes bucket at value (a char) and increments by 1
}

void characterBucket::printAll(){
    if(this->ignoreCaps){

    }

}


  




numberList::numberList(bool isIntList){
    this->isIntegerList = isIntList;
};



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

