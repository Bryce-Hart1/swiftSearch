#include "constants.hpp"

#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

int returnCountOf(std::string filename, std::string keyWord, char breakOperand);

bool wasFoundSkipWord(std::string filename, std::string keyWord, char breakOperand);

bool wasFound(std::string filename, std::string keyWord, char breakOperand);

std::vector<double> collectNumbers(std::string filename);




std::vector<std::thread> assignThread(OP_TYPE opertation, std::string fileName);

void joinThreads(std::vector<std::thread> threadVec, int threadNumber);
