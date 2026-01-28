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
#include <cmath>


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
    this->isRoot = true;
}

//gets called on all children
atomicNode::atomicNode(char value){
    this->value = value;
    this->isRoot = false;
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
    atomicNode* current = this;
    
    for(int i = 0; i < word.length(); i++){
        if(NO_CAPITALS_FLAG){ //first check if capitals are allowed. If not, set to lower
            word[i] = toLower(word[i]);
        }
        std::lock_guard<std::mutex> lock(current->mtx);

        atomicNode* thisChild = nullptr; 
        for(auto& c : current->children){ //check all children in vector
            if(c->getValue() == word[i]){
                thisChild = c.get();
                break;
            }
        }


        if(thisChild == nullptr){ //if none was found, assignment doesnt change and we create one here
            children.emplace_back(std::make_unique<atomicNode>(value));
            //children.push_back(std::make_unique<atomicNode>(value)); might be worth testing but since both are created inside make_unique idk
        }
        current = thisChild;
    }
    current->setEndPointTrue();
}

//print everything but root, and only if it is an endpoint
void atomicNode::printSet(std::string prefix){
    if(!this->isEndPoint && !this->isRoot){
        std::println(std::cout,"{} : {}", prefix, this->count);
    }
    for(auto& child : children){ //for all children
        child->printSet(prefix + child->value);
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
 * @details does a opertation to add characters to buckets to print the count of them later
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
                    std::println(std::cout, "{}: count: {}", getValueAt(i), (getCounterAt(i) + getCounterAt(i-dist)) );
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
 * @class numberList
 * wraps over std::vector and just picks between a long long int or a long double
 */
numberList::numberList(bool isIntList){
    if(isIntList){
        this->setType(LIST_TYPE::INT);
    }else{
        this->setType(LIST_TYPE::DOUBLE);
    }
}
void numberList::add(auto data){
    if(this->isIntTypeList()){
        data = static_cast<long long int>(data); //just to be safe
        this->iList.push_back(data);
    }else{
        data = static_cast<long double>(data);
        this->dList.push_back(data);
    }
}

void numberList::setType(LIST_TYPE x){
    this->type = x;
}

bool numberList::isIntTypeList(){
    if(this->type == LIST_TYPE::INT){
        return true;
    }
    return false;
}

std::vector<long long int> numberList::sendIntVector(){
    return this->iList;
}

std::vector<long double> numberList::sendDoubleVector(){
    return this->dList;
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
    

void Timer::reset() {
    startTime = std::chrono::high_resolution_clock::now();
    running = true;
}
    
void Timer::printCurrentTime() const {
    return;
}
  
//returns a string type in a Digits - measurement format. no spacing. up to minutes
std::string Timer::readableTime(){
    long double timeElapsed = (long double)this->timeInMilliseconds();
    if(timeElapsed < 1000.0){ //less than one second (1000 milliseconds)
        return (std::to_string(timeElapsed) + " ms");
    }
    timeElapsed /= 1000; //now in seconds format
    if(timeElapsed < 60){ //if less than a minute
        return (std::to_string(timeElapsed) + " s");
    }
    timeElapsed /= 60; //now in minutes
    return (std::to_string(timeElapsed) + " min");
}






file::file(){
    this->fileName = "Unassigned";
}

//root shoul;dnt matter with new design
file::file(std::string name){
    try{
        this->fileName = name;
        this->fsPath = std::filesystem::path(name);
    }catch(std::exception e){
        std::cerr << e.what() << std::endl;
    }
}


std::string file::getFileName(){
    return (this->fileName);
}

std::string file::filePathToStr(){
    return (this->fsPath.string());
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

//returns path type
std::filesystem::path file::getfsPath(){
    return(this->fsPath);
}

void file::setFileName(std::string name){
    this->fileName = name;
}
void file::setFileSize(std::uintmax_t size){
    this->sizeOfFile = size;
}
void file::setEnumType(std::filesystem::file_type type){
    this->enumFileType = type;
}
void file::setPath(std::filesystem::path path){
    this->fsPath = path;
}




/**
 * @class fileTreeStructure
 * is not actually a tree, is a 1d vector that has no ordered values
 * to create a fileTree, a given root is scanned using file 
 */

//creates whole tree from the root file given
fileTreeStructure::fileTreeStructure(file root){
    namespace fs = std::filesystem;
    try{ //first create structure from the filesystem iterator
        for(const auto &entry : std::filesystem::recursive_directory_iterator(root.getfsPath())){
            file x;
            x.setFileName(entry.path().filename().generic_string());
            x.setPath(entry.path());
            x.setEnumType(entry.status().type());

            if(entry.is_regular_file()){ //dont wanna set size for anything that isnt a normal file
                x.setFileSize(entry.file_size());
            }else{
                x.setFileSize(0); //no size
            }
            storedFiles.push_back(x);
        }

    }catch(std::exception e){
        std::cerr << e.what() << std::endl;
    }
}

//returns string of name of individal file
std::string fileTreeStructure::getNameAt(int value){
    return (this->storedFiles.at(value).getFileName());
}


//returns enum, not string type
std::filesystem::file_type fileTreeStructure::returnEnumTypeAt(int value){
    return (this->storedFiles.at(value).getEnumType());
}


//returns string type from path type
std::string fileTreeStructure::getPathAt(int value){
    return (this->storedFiles.at(value).getfsPath().generic_string());
}

//converts enum type to a readable string
std::string fileTreeStructure::fileTypeToString(std::filesystem::file_type type) {
    namespace fs = std::filesystem;
    switch(type) {
        case fs::file_type::regular:    return "regular file";
        case fs::file_type::directory:  return "directory";
        case fs::file_type::symlink:    return "symlink";
        case fs::file_type::block:      return "block device";
        case fs::file_type::character:  return "character device";
        case fs::file_type::fifo:       return "fifo";
        case fs::file_type::socket:     return "socket";
        case fs::file_type::unknown:    return "unknown";
        case fs::file_type::not_found:  return "not found";
        default:                                     return "none";
    }
}


//creates a queue of nodes to traverse based off of the orginal vector
std::queue<file> fileTreeStructure::createFileQueue(){
    std::queue<file> names; 
    for(file f : this->storedFiles){
        names.push(f);
    }
    return names;
}

size_t fileTreeStructure::entrysInStruct(){
    return this->storedFiles.size();
}


//only prints name of file and path. for debug 
void fileTreeStructure::printAll(){
    for(auto& f : this->storedFiles){
        //std::println(f.getFileName());
        std::println(std::cout, "path: {}", f.filePathToStr());
    }
}