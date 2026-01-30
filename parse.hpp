#include "constants.hpp"
#include "classes.hpp"

#include<vector>
#include<thread>


void findAll(std::string filename, std::string keyWord);

bool findOne(std::string filename, std::string keyWord);

void wordFreq(std::string fileName, atomicNode &wordTree);

void charFreq(std::string fileName, characterBucket &cBucket);

numberList singleList(std::string fileName);

void assignOperation(OP_TYPE opertation, std::queue<file> fileNames);

void joinThreads(std::vector<std::thread> &threadVec);