#include "classes.hpp"
#include "constants.hpp"
#include "parse.hpp"

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
#include <future>
#include <algorithm>
#include <fstream>
#include <unordered_map>

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
    return this->value;
}

//endpoint just means that it is a word/phrase that needs saved

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
    
    for(size_t i = 0; i < word.length(); i++){
        char ch = word[i];
        if(NO_CAPITALS_FLAG){
            ch = toLower(ch);
        }
        
        std::lock_guard<std::mutex> lock(current->mtx); //lock current
        
        atomicNode* thisChild = nullptr;
        
        for(auto& c : current->children){
            if(c->value == ch){
                thisChild = c.get();
                break;
            }
        }
        
        if(thisChild == nullptr){
            current->children.emplace_back(std::make_unique<atomicNode>(ch));
            thisChild = current->children.back().get();

        }
        
        current = thisChild;
    }
    
    //these need to be outside any lock since they acquire their own locks anyway
    current->setEndPointTrue();
    current->increment();
}

//print everything but root, and only if it is an endpoint
void atomicNode::saveSet(std::string prefix, std::vector<std::string>& allwords){
    if(this->isEndPoint && !this->isRoot){
        allwords.push_back(prefix);
    }
    for(auto& child : children){ //for all children
        child->saveSet(prefix + child->value, allwords);
    }
}

void atomicNode::printTofile(std::string predef, std::vector<std::string>& words){
    std::ofstream output(predef);
    if(!output.is_open()){
        print::Debug("Error in numberList::printToFile: could not open file.");
        return;
    }
    print::Debug("Amount of words to print to file: " + std::to_string(words.size()));
    size_t counter = 0;
    std::string prev = "";
    for(auto& word : words){
        if(prev == word){
            prev = word;
            counter++;
        }else{ //else, we print the current amount we have (could be one ) and reset counter and update previous
            output << word << ":" << std::to_string(counter) << std::endl;
            prev = word;
            counter = 1; //because if we reset again then we need to show that at least one instance was shown
        }
    }
}




/**
 * @class partners with characterBucket
 * holds a set of values 
 * [a]
 * [1]
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
void atomCharPair::setIntialValue(){
    this->count = 0;
}


/**
 * @class characterBucket - stores counts of all characters found
 * @details does a opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * holds in array style, where everytime an element is accessed by the value of its char
 * [a][b][c][d][e]...
 * [1][0][9][3][5]...
 * MycharacterBucket.addTo(b);
 * [a][b][c][d][e]...
 * [1][1][9][3][5]...
 */
characterBucket::characterBucket(){
    this->ignoreCaps = NO_CAPITALS_FLAG;
    for(size_t i = 0; i < this->buckets.size(); i++){
        buckets.at(i).setIntialValue();
    }
}


void characterBucket::addTo(char value){
    this->buckets.at(value).increment(); //takes bucket at value (a char) and increments by 1
}

void characterBucket::printAll(){
    using namespace std;
    for(size_t i = 0; i < this->buckets.size(); i++){
        if(getCounterAt(i) > 0){
            if(ignoreCaps){
                const int distance = 32;
                if(i >= 'A' && i <= 'Z'){
                    continue; 
                }
                else if(i >= 'a' && i <= 'z'){
                    println(cout, "{}: count: {}", (char)i, getCounterAt(i) + getCounterAt(i - distance));
                }
                else{ //print non alphabet
                    println(cout, "{}: count: {}", (char)i, getCounterAt(i));
                }
            }else{ // no ignore flag, just print all of them
                println(cout, "{}: count: {}", (char)i, getCounterAt(i));
            }
        }
    }
}

char characterBucket::getValueAt(int value){
    return (this->buckets.at(value).getValue());
}

size_t characterBucket::getCounterAt(int value){
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
numberList::numberList(){
    if(!FLOAT_NUMBER_LIST_FLAG){
        this->setType(LIST_TYPE::INT);
    }else{
        this->setType(LIST_TYPE::DOUBLE);
    }
}

void numberList::add(long long int data){
    iList.push_back(data);
}
void numberList::add(long double data){
    dList.push_back(data);
}



void numberList::addVec(numberList vector){
    if(this->isIntTypeList()){
        for(auto& a : vector.iList){ //pull from that vectors int list
            this->iList.push_back(a);
        }
    }else{
        for(auto& a : vector.dList){
            this->dList.push_back(a);
        }
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

void numberList::sort(){
    if(isIntTypeList()){
        std::sort(iList.begin(), iList.end());
    }else{
        std::sort(dList.begin(), dList.end());
    }
}


void numberList::reverse(){
    if(isIntTypeList()){
        for(size_t i = 0; i < iList.size()/2; i++){
            long long int toFront = iList[iList.size() - i];
            iList[iList.size() - i] = iList[i];
            iList[i] = toFront;
        }
    }else{
        for(size_t i = 0; i < dList.size(); i++){
            long double toFront = dList[dList.size() - i];
            dList[dList.size() - 1] = dList[i];
            dList[i] = toFront;
        }
    }

}

void numberList::printList(){
    if(isIntTypeList()){ //these prints are temp, will move to printing to file format
        for(auto& i : iList){
            std::cout << i <<std::endl;
        }
    }else{
        for(auto& d : dList){
            std::cout << d << std::endl;
        }
    }
}

//call this method last. Takes a suggested file name
void numberList::printToFile(std::string preDefFileName){
    std::ofstream output(preDefFileName);
    if(!output.is_open()){
        print::Debug("Error in numberList::printToFile: could not open file.");
        return;
    }
    if(isIntTypeList()){
        for(auto& a : iList){
            output << std::to_string(a) << std::endl;
        }
    }else{
        for(auto& a : dList){
            output << std::to_string(a) << std::endl;
        }
    }
    output.close();
    print::Debug("Data has been written to " + preDefFileName);
}


/**
 * combines all vectors from singleLists() and waits for all threads to finish, and combines them
 * @details passes in file class, not string
 */
numberList numberListHelper::combinedList(std::queue<file> files){
    numberList mainList{};
    std::vector<std::future<numberList>> futureList;

    while(!files.empty()){
        futureList.push_back(std::async(singleList, files.front())); //create a task for each vector position
        files.pop();
    }
    for(size_t i = 0; i < futureList.size(); i++){
        mainList.addVec(futureList[i].get()); //get vector at that vector position, and add to the main vector
    }
    return mainList; //return the full list 
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
    }catch(const std::exception& e){
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
        return (std::to_string(fileSize) + " kilobytes");
    }else{ 
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

    }catch(const std::exception& e){
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