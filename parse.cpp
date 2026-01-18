#include"parse.hpp"
#include"constants.hpp"
#include<filesystem>
#include<vector>
#include<iostream>
#include<string>
#include<fstream>
#include<exception>
#include<thread>





void info(std::string fileName){

}


/**
 * @brief collects numbers in file and sends them to merge with the other threads
 * 
 * 
 * 
 */
std::vector<double> collectNumbers(std::string fileName){
    


}

/**
 * @details 
 * @param filename name of the file the thread will work on
 * @param keyWord will return this count of this string to the shared memory 
 * 
 */
int returnCountOf(std::string filename, std::string keyWord, char breakOperand){
    using namespace std;

    int countInFile = 0;

    ifstream inputFile(filename);
    if (!inputFile.is_open()) {
        //throw customExceptions("File " + filename + " failed to open.\n");}




string line, currentword;
while(getline(inputFile, line)){
    for(int i = 0; i < line.length(); i++){
        if(line[i] == breakOperand){
            currentword.clear();
        }
        currentword+= line[i];
        }
    }

}

}




int findAll(std::string fileName, std::string keyWord){

}

/**
 * @brief finds and returns as soon as keyword is found, even if keyword is inside of another word
 * 
 * 
 */
bool findOne(std::string filename, std::string keyWord, char breakOperand){
    using namespace std;

    ifstream inputFile(filename);
if (!inputFile.is_open()) {
    //throw customExceptions("File " + filename + " failed to open.\n");
}

string line;
int SizeOfKeyWord = keyWord.length();
int needInd = 0;
while(getline(inputFile, line)){
    for(int i = 0; i < line.length(); i++){
        if(keyWord[needInd] != line[i]){ //if the char we are looking for is not the same as the one on string
            needInd = 0;
        }else{
            needInd++; //match move to next ind
        }
        if(needInd == SizeOfKeyWord){
            return true;
        }
    }

    }
    return false;
}






auto wordFreq(std::string fileName){

}



auto charFreq(std::string fileName){
    
}




/**
 * @brief creates new vector of threads depending on what is needed
 * @param threadNumber the number in the threadArray
 * @param operation the opertation that is done by the thread, matches all other threads
 * @param fileName name of the file that the opertation will be done on, different for every thread
 */

std::vector<std::thread> assignThread(OP_TYPE opertation, std::string fileName){
    switch(opertation){
            case OP_TYPE::INFO:
            //std::thread tempThread()
            break;
            case OP_TYPE::LIST_NUMBERS:
            case OP_TYPE::R_SORTED_LIST:
            case OP_TYPE::SORTED_LIST:
            //std::thread temp(collectNumbers(fileName));
            break;
            case OP_TYPE::CHAR_FREQ:
            //std::thread temp() not yet implemented
            break;
            case OP_TYPE::WORD_FREQ:
            //not yet implemented
            break;
            case OP_TYPE::FIND_ALL:
            //find all instances of string and return
            break;
            case OP_TYPE::FIND_ONE:
            //find one instance of string and return
            break;
            default:
            
        }
    }
    




void joinThreads(std::vector<std::thread> threadVec, int threadNumber){
}

