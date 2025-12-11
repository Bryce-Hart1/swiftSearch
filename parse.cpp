#include<parse.h>
#include<filesystem>
#include<vector>
#include<iostream>
#include<string>
#include<fstream>
#include<exception>
#include<thread>


/**
 * @brief displays the files that will be tranversed to user
 * @param directory 
 * 
 * 
 */

void displayFileTree(FileTreeNode directory){
    std::cout << "Processing Following files: " << std::endl;
    if(directory.children.size() < 100){ //will display
        for(int i = 0; i < directory.children.size(); i++){
            std::cout << "[" << (i+1) << "]" << directory.children[i] << ", ";

            if(i % 10 == 0){
                std::cout << std::endl;
            }
        }
    }

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
        throw customExceptions("File " + filename + " failed to open.\n");}




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




/**
 * @brief finds and returns keyword only if keyword has breakOperand on both sides.
 * 
 * 
 * 
 * 
 */

bool wasFoundSkipWord(std::string filename, std::string keyWord, char breakOperand){
    using namespace std;

    ifstream inputFile(filename);
if (!inputFile.is_open()) {
    throw customExceptions("File " + filename + " failed to open.\n");
}
string line;
string needed;
int neededInd = 0;
while(getline(inputFile, line)){
    for(int i = 0; i < line.length(); i++){
        if(line[i] == breakOperand){
            needed = keyWord;
        }else{

        }
        
    }

    }
}



/**
 * @brief finds and returns as soon as keyword is found, even if keyword is inside of another word
 * 
 * 
 */
bool wasFound(std::string filename, std::string keyWord, char breakOperand){
    using namespace std;

    ifstream inputFile(filename);
if (!inputFile.is_open()) {
    throw customExceptions("File " + filename + " failed to open.\n");
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


/**
 * @brief collects numbers in file and sends them to merge with the other threads
 * 
 * 
 * 
 */
std::vector<double> collectNumbers(std::string fileName){
    


}







/**
 * @param threadArray the predefined array of threads
 * @param threadNumber the number in the threadArray
 * @param operation the opertation that is done by the thread, matches all other threads
 * @param fileName name of the file that the opertation will be done on, different for every thread
 */



void assignThread(std::thread threadArray[], int threadNumber, int opertation, std::string fileName){

    
}




void joinThreads(std::thread threadArray[], int threadNumber){
    threadArray[threadNumber].join();
}

