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


void atomicNode::add(std::string word){
    //find out how to assign root 
    for(int i = 0; i < word.length(); i++){
        if(this->findChild(word[i]) != nullptr){ //if there is an existing child, contine
            
        }
    }
}







/**
 * @class partners with characterBucket
 */
void atomCharPair::increment(){
    this->count.fetch_add(1);
}

int atomCharPair::getCount(){
    return (this->count);
}
char atomCharPair::getValue(){
    return (this->value);
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
    for(int i = 0; i < this->buckets.size(); i++){
        if(ignoreCaps){
            const int dist = 32; //distance from A to a in ascii
            if(i < 'A' && 'Z' < i){//better A and Z print nothing
                if('a' <= i && i <= 'z'){ //between a and z, add counts from the capitals
                    std::println("{}: count: {}", getValueAt(i), (getCounterAt(i) + getCounterAt(i-dist)) );
                }
            }
        }
    }

}

char characterBucket::getValueAt(int value){
    return (this->buckets.at(value).getValue());
}

unsigned int characterBucket::getCounterAt(int value){
    return (this->buckets.at(value).getCount());
}



/**
 * @class an atomic counter.
 * use getCount to get count from a value, and incrementBy to change the count by a given amount.
 */


unsigned long long int simpleCount::getCount(){
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
    


}


std::string file::returnFileName(){
    return (this->fileName);
}

//returns size in bytes
unsigned long long int file::returnFileSize(){
    return static_cast<unsigned long long int>(this->sizeOfFile);
}


//returns a readable size for the user
std::string file::returnReadableSize(){
    unsigned long long int fileSize = (this->returnFileSize());
    if( fileSize < 1000 ){
        return (std::to_string(fileSize) + " bytes"); 
    }else if(fileSize < 1000000){ //up to giga
        double temp = (fileSize / 1000);
        return (std::to_string(fileSize) + " kilobytes");
    }else{ 
        double temp = (fileSize / 1000000);
        return (std::to_string(fileSize) + " gigabytes");  
    }
}

fileTreeStructure::fileTreeStructure(file root){
    
}

std::string fileTreeStructure::getNameAt(int value){
    return (this->storedFiles.at(value).returnFileName());
}

std::filesystem::file_type fileTreeStructure::returnEnumTypeAt(int value){
    return (this->storedFiles.at(value).returnEnum());
}


std::string fileTreeStructure::fileTypeToString(std::filesystem::file_type type) {
    //ill do a namespace later to clean this actual eyesore
    switch(type) {
        case std::filesystem::file_type::regular:    return "regular file";
        case std::filesystem::file_type::directory:  return "directory";
        case std::filesystem::file_type::symlink:    return "symlink";
        case std::filesystem::file_type::block:      return "block device";
        case std::filesystem::file_type::character:  return "character device";
        case std::filesystem::file_type::fifo:       return "fifo";
        case std::filesystem::file_type::socket:     return "socket";
        case std::filesystem::file_type::unknown:    return "unknown";
        case std::filesystem::file_type::not_found:  return "not found";
        default:                                     return "none";
    }
}

std::queue<std::string> fileTreeStructure::createStringQueue(){
    std::queue<std::string> names;
    for(file f : this->storedFiles){
        names.push(f.returnFileName());
    }
    return names;
}
