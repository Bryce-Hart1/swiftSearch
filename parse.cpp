#include "parse.hpp"
#include "constants.hpp"
#include "classes.hpp"

#include<filesystem>
#include<vector>
#include<iostream>
#include<string>
#include<fstream>
#include<exception>
#include<thread>





void info(file x){
    std::println("File name: {} | File size: {}", x.returnFileName());

}


/**
 * @brief collects numbers in file and sends them to merge with the other threads
 * 
 * 
 * 
 */





bool check(std::string lookFor, std::string current){
    if(lookFor.length() == current.length()){

    }
    return false;
}

/**
 * @details 
 * @param filename name of the file the thread will work on
 * @param keyWord will return this count of this string to the shared memory, which is just a shared count
 * 
 */
void findAll(std::string filename, std::string keyWord){
    using namespace std;

    simpleCount counter;

    string line;

    try{
    ifstream inputFile(filename);
    if(inputFile.is_open()){
        while(getline(inputFile, line)){
            istringstream strstream(line);
            string word;

            while(strstream >> word){
                if(check(keyWord, word)){
                    counter.incrementBy(1);
                }

            }
        }
    }



    }catch(std::filesystem::filesystem_error e){
        println(e.what());

        printDebug("Error opening file");
    }
}
    




/**
 * @brief finds and returns as soon as keyword is found, even if keyword is inside of another word
 * 
 * 
 */
bool findOne(std::string fileName, std::string keyWord){




    return true; //placeHolder
}





//should make a file since the amount of words could be very large
void wordFreq(std::string fileName){

}


/**
 * @brief using charbucket to find character frequency.
 * As of right now, it does not track spaces but this is by design.
 * 
 * 
 */
void charFreq(std::string fileName, characterBucket &cBucket){


try{
    std::ifstream input(fileName);
    std::string word;

    while(input >> word){
        for(char letter : word){
            cBucket.addTo(letter);
        }
    }



}catch(std::exception e){
    std::println(e.what());
}


}






/**
 * @brief creates new vector of threads depending on what is needed
 * @param threadNumber the number in the threadArray
 * @param operation the opertation that is done by the thread, matches all other threads
 * @param fileName name of the file that the opertation will be done on, different for every thread
 */


void assignOperation(std::vector<std::thread> &threadVector, OP_TYPE opertation, std::queue<std::string> fileNames){

        std::thread thread;
        switch(opertation){
            case OP_TYPE::INFO :{



            break;
            }case OP_TYPE::LIST_NUMBERS: {
                //not let implemented
                break;
            }
            case OP_TYPE::R_SORTED_LIST: {

            }case OP_TYPE::SORTED_LIST: {
            break;

            }case OP_TYPE::CHAR_FREQ: {
                characterBucket bucketArr(NO_CAPITALS_FLAG);
                while(!fileNames.empty()){
                    std::thread thread(charFreq(fileNames.front(),bucketArr));
                    fileNames.pop();
                }


            }case OP_TYPE::WORD_FREQ: {
            //not yet implemented
            break;
            }case OP_TYPE::FIND_ALL: {
                std::thread temp(findAll);
            break;
            }case OP_TYPE::FIND_ONE: {
            //find one instance of string and return
            break;
            }default:
            break;
        }

    }
    




void joinThreads(std::vector<std::thread> threadVec, int threadNumber){
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(std::exception e){

        }
    }
    printDebug("Threads have finished");
}

