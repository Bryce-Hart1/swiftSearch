#include<filesystem>
#include<vector>
#include<thread>


//if you are looking for fileTreeNode class it is in tokenize, do not write related functions here

int returnCountOf(std::string filename, std::string keyWord, char breakOperand);

bool wasFoundSkipWord(std::string filename, std::string keyWord, char breakOperand);

bool wasFound(std::string filename, std::string keyWord, char breakOperand);

std::vector<double> collectNumbers(std::string filename);

void assignThread(std::thread threadArray[], int threadNUmber, int opertation, std::string filename);

void joinThreads(std::thread threadArray[], int threadNumber);