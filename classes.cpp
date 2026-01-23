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



//fast toLower function. may need it later
char toLower(unsigned char value) {
    if(value >= 'A' && value <= 'Z') {
        return value + 32;
    }
    return value;
}


//default constructor gets called on root
atomicNode::atomicNode(){
    this->value = '*';
    this->root = true;
}

//gets called on all children
atomicNode::atomicNode(char value){
    this->value = value;
    this->root = false;
}
//increment the count for this endNode
void atomicNode::increment() {
    if(this->getIsEndPoint()){
    std::lock_guard<std::mutex> lock(mtx);
    ++count;
    }
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

bool atomicNode::getIsEndPoint(){
    return this->isEndPoint;
}

//creates new child node for the current node
void atomicNode::createChildNodeFor(char value){
    std::lock_guard<std::mutex> lock(mtx); //lock current 
    (this->children.emplace_back(atomicNode(value))); //create new node at vector pos
}


//returns a node at requested position, if not returns nullptr
atomicNode* atomicNode::findChildNode(char value){
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


//adds entire word
void atomicNode::add(std::string word){
    
    for(int i = 0; i < word.length(); i++){
        if(NO_CAPITALS_FLAG){ //first check if capitals are allowed. If not, set to lower
            word[i] = toLower(word[i]);
        }
        if(this->findChildNode(word[i]) == nullptr){ //if there is an existing child, contine
            createChildNodeFor(word[i]); //if not, create one here
        }

    }
}

//print everything but root, and only if it is an endpoint
void atomicNode::printTree(){

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
    this->fileName = name;
    this->fsPath = std::filesystem::path(name);
}


std::string file::getFileName(){
    return (this->fileName);
}

//returns size in bytes
unsigned long long int file::getFileSize(){
    return static_cast<unsigned long long int>(this->sizeOfFile);
}


//returns a readable size for the user
std::string file::returnReadableSize(){
    unsigned long long int fileSize = (this->getFileSize());
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

//returns enum of file type from file_type enum
std::filesystem::file_type file::getEnumType(){
    return (this->enumFileType);
}

std::filesystem::path file::getfsPath(){
    return(this->fsPath);
}




fileTreeStructure::fileTreeStructure(file root){
    
}

std::string fileTreeStructure::getNameAt(int value){
    return (this->storedFiles.at(value).getFileName());
}

std::filesystem::file_type fileTreeStructure::returnEnumTypeAt(int value){
    return (this->storedFiles.at(value).getEnumType());
}



std::string fileTreeStructure::getPathAt(int value){
    return (this->storedFiles.at(value).getfsPath());
}
std::string fileTreeStructure::fileTypeToString(std::filesystem::file_type type) {

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
        names.push(f.getFileName());
    }
    return names;
}
