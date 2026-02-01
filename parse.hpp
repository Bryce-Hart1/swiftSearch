#include "constants.hpp"
#include "classes.hpp"
#include "tokenize.hpp"

#include<vector>
#include<thread>


void findAll(file filename, std::string keyWord);

bool findOne(file filename, std::string keyWord);

void wordFreq(file fileName, atomicNode &wordTree);

void charFreq(file fileName, characterBucket &cBucket);

numberList singleList(file f);

void assignOperation(OP_TYPE opertation, std::queue<file> fileNames);

void joinThreads(std::vector<std::thread> &threadVec);