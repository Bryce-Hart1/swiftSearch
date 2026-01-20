#include "constants.hpp"
#include "classes.hpp"

#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

void info(file fileName);

std::vector<double> collectNumbers(file filename);


bool check(std::string lookFor, std::string current);

void findAll(file filename, std::string keyWord);

bool findOne(file filename, std::string keyWord);

void wordFreq(file fileName);

void charFreq(file fileName);

void assignThreads(std::vector<std::thread> &threadVec, OP_TYPE opertation, std::queue<std::string> fileNames);

void joinThreads(std::vector<std::thread> threadVec, int threadNumber);
