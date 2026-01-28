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

/**
 * @details 
 * @param filename name of the file the thread will work on
 * @param keyWord will return this count of this string to the shared memory, which is just a shared count
 * 
 */
void findAll(std::string filename, std::string keyWord){
    print::Thread("findAll entered", filename);
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
                if(keyWord == word){
                    counter.incrementBy(1);
                }

            }
        }
    }


    }catch(std::filesystem::filesystem_error e){
        print::Error(e);
    }
}
    




/**
 * @brief finds and returns as soon as keyword is found, even if keyword is inside of another word
 * 
 */
bool findOne(std::string fileName, std::string keyWord){
    print::Thread("Entering findOne", fileName);
    try{
        std::ifstream input(fileName);
        std::string word;
        while(input >> word){
            if(word == keyWord){
                return true;
            }
        }
    }
    catch(std::exception& e){
        print::Error(e);
    }

    return false; 
    print::Thread("Exiting findOne", fileName);
}





//should make a file since the amount of words could be very large
void wordFreq(std::string fileName, atomicNode &wordTree){
    print::Thread("Entering wordFreq", fileName);
    using namespace std;
    try{
        ifstream input(fileName);
        string word;

        while(input >> word){
            wordTree.add(word);
        }
    }catch(exception e){
        string error = e.what();
        print::Debug(error);
    }
}


/**
 * @brief using charbucket to find character frequency.
 * As of right now, it does not track spaces but this is by design.
 * 
 * 
 */
void charFreq(std::string fileName, characterBucket &cBucket){
    print::Thread("charFreq entered", fileName);
    try{
        std::ifstream input(fileName);
        std::string word;

        while(input >> word){
            for(char letter : word){
                cBucket.addTo(letter);
            }
        }
    }catch(std::exception e){
        print::Error(e);
    }
    print::Thread("charFreq exiting", fileName);
}






/**
 * @brief creates new vector of threads depending on what is needed
 * @param threadNumber the number in the threadArray
 * @param operation the opertation that is done by the thread, matches all other threads
 * @param fileName name of the file that the opertation will be done on, different for every thread
 */


void assignOperation(std::vector<std::thread> &threadVector, OP_TYPE opertation, std::queue<file> filesList){
    print::Debug("assignOperation() entered.");
        switch(opertation){
            case OP_TYPE::INFO :{ //making this simple operation run through on a single loop
                print::Debug("Operation Type: INFO has started");
                while(!filesList.empty()){
                    std::println(std::cout, "FileName {} | Size: {} | Path: {}", 
                        filesList.front().getFileName(), 
                        filesList.front().returnReadableSize(), 
                        filesList.front().filePathToStr());
                    filesList.pop();
                }
                print::Debug("INFO has finished.");
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
                while(!filesList.empty()){
                    threadVector.emplace_back(charFreq, filesList.front().getFileName(), std::ref(bucketArr));
                    filesList.pop();

                }
                std::string charMes = ("queue sucessfully finished for charFreq vector size is" + std::to_string(threadVector.size()));
                print::Debug(charMes);
                joinThreads(threadVector); //seperate join function
                bucketArr.printAll(); //print with method

            }case OP_TYPE::WORD_FREQ: {
                atomicNode wordTree('*'); //root
                while(!filesList.empty()){
                    threadVector.emplace_back(wordFreq, filesList.front().getFileName(), std::ref(wordTree));
                    filesList.pop();
                }
                print::Debug("Queue sucessfully created with size" + std::to_string(threadVector.size()));

            //not yet implemented
            break;
            }case OP_TYPE::FIND_ALL: {
                //std::thread temp(findAll); copy for what we have above, ref class by ref and emplace back
            break;
            }case OP_TYPE::FIND_ONE: {
            //find one instance of string and return
            break;
            }default:
            break;
        }

    }
    




void joinThreads(std::vector<std::thread>& threadVec){
    print::Debug("joinThreads() Entered");
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(std::exception e){
            std::string error = e.what();
            print::Debug(error);
        }
    }
    print::Debug("Threads have finished");
}