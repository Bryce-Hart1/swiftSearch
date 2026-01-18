#include "constants.hpp"

#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

void info(file fileName);

std::vector<double> collectNumbers(file filename);


bool check(std::string lookFor, std::string current);

int findAll(file filename, std::string keyWord);

bool findOne(file filename, std::string keyWord);

void wordFreq(file fileName);

void charFreq(file fileName);


std::vector<std::thread> assignThread(OP_TYPE opertation, std::string fileName);

void joinThreads(std::vector<std::thread> threadVec, int threadNumber);
