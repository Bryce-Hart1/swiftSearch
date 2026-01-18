#include "constants.hpp"

#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

void info(std::string fileName);

std::vector<double> collectNumbers(std::string filename);


int findAll(std::string filename, std::string keyWord);

bool findOne(std::string filename, std::string keyWord, char breakOperand);

auto wordFreq(std::string fileName);

auto charFreq(std::string fileName);


std::vector<std::thread> assignThread(OP_TYPE opertation, std::string fileName);

void joinThreads(std::vector<std::thread> threadVec, int threadNumber);
