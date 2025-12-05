#include<tokenize.cpp>




unsigned int totalCount;
std::vector<double> sortableVector;
std::mutex sharedMutex;





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



std::vector<double> sendSort(std::string fileName){

}

