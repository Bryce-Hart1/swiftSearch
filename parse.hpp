#include "constants.hpp"
#include "classes.hpp"

#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

void info(file fileName);


bool check(std::string lookFor, std::string current);

void findAll(std::string filename, std::string keyWord);

bool findOne(std::string filename, std::string keyWord);

void wordFreq(std::string fileName);

void charFreq(std::string fileName, characterBucket &cBucket);

void assignOperation(std::vector<std::thread> &threadVec, OP_TYPE opertation, std::queue<std::string> fileNames);

void joinThreads(std::vector<std::thread> &threadVec);
