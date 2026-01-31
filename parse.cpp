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
    print::Thread(str::enter, filename);
    using namespace std;
    int rowOffile = 0;
    simpleCount counter;

    string line;

    try{
    ifstream inputFile(filename);
        if(inputFile.is_open()){
            while(getline(inputFile, line)){
                istringstream strstream(line);
                string word;
                rowOffile++;
            while(strstream >> word){
                if(keyWord == word){
                    counter.incrementBy(1);
                    print::Found(keyWord, filename, counter.getCount(), rowOffile);
                }

            }
        }
    }


    }catch(const std::filesystem::filesystem_error& e){
        print::Error(e);
    }
    print::Thread(str::exit, filename);
}
    




/**
 * @brief finds and returns as soon as keyword is found, even if keyword is inside of another word
 * 
 */
bool findOne(std::string fileName, std::string keyWord){
    print::Thread(str::enter, fileName);
    try{
        std::ifstream input(fileName);

        std::string word;
        while(input >> word){
            if(word == keyWord){
                return true;
            }
        }
    }
    catch(const std::exception& e){
        print::Error(e);
    }

    return false; 
    print::Thread(str::exit, fileName);
}



//should make a file since the amount of words could be very large
void wordFreq(std::string fileName, atomicNode &wordTree){
    print::Thread(str::enter, fileName);
    using namespace std;
    try{
        ifstream input(fileName);
        string word;

        while(input >> word){
            wordTree.add(word);
        }
    }catch(const exception& e){
        string error = e.what();
        print::Debug(error);
    }
    print::Thread(str::exit, fileName);
}


/**
 * @fn charFreq 
 * @brief using charbucket to find character frequency.
 * puts all characters found in the file into a combined charbucket
 * As of right now, it does not track spaces but this is by design.
 */
void charFreq(std::string fileName, characterBucket &cBucket){
    print::Thread(str::enter, fileName);
    try{
        std::ifstream input(fileName);
        std::string word;

        while(input >> word){
            for(char letter : word){
                cBucket.addTo(letter);
            }
        }
    }catch(const std::exception &e){
        print::Error(e);
    }
    print::Thread(str::exit, fileName);
}

/**
 * @fn makes input from a single thread and gives it to the main numberList
 * works with both vectors in numberList
 */
numberList singleList(file f){
    long long debugCount = 0;
    print::Thread(str::enter, f.getFileName());
    numberList list{}; //returned list
    try{
        std::ifstream input(f.filePathToStr());
        if(!input.is_open()){
            print::Debug("File " + f.getFileName() + "Failed to open.");
        }
            std::string word;
        while(input >> word){
        if(FLOAT_NUMBER_LIST_FLAG){
            try{
                size_t pos;
                long double num = std::stold(word, &pos);
                if(pos == word.length()){
                    list.add(num); //only adds if there is no trailing characters
                    debugCount++;
            }
        }catch(const std::invalid_argument&){
        }catch(const std::out_of_range&){
            print::Debug("The number found was out of range. ");
        }
    }else{
        try{
            size_t pos;
            long long int num = std::stoll(word, &pos);
            if(pos == word.length()){
                list.add(num);
                debugCount++;
            }
        }catch(const std::invalid_argument&){
        }catch(const std::out_of_range&){
            print::Debug("The number found was out of range.");
        }
    }
}
        
    }catch(const std::exception& e){
        print::Error(e);
    }
    print::Debug(std::to_string(debugCount) + " numbers found in " + f.getFileName());
    print::Thread(str::exit, f.getFileName());
    return list;
}



/**
 * @fn assignOpertation 
 * @brief does most of the main operation work at runtime, including assigning opertations through the switch,
 * doing the related work with that operation, and eventually returning after using @fn joinThreads if needed
 * @param operation previously set operation type 
 * @param filesList queue of files to run through
 */

void assignOperation(OP_TYPE operation, std::queue<file> filesList){
    std::vector<std::thread> threadVector;

    print::Debug("assignOperation() entered.");
        switch(operation){
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
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.printList(); //eventually will have printable file format
                mainList.printToFile("ListedNumbers.txt");
                break;
            }
            case OP_TYPE::R_SORTED_LIST: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.sort();
                mainList.reverse();
                mainList.printToFile("ReverseSortedList.txt");
                break;
            }case OP_TYPE::SORTED_LIST: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.sort();
                mainList.printToFile("SortedList.txt");
                std::cout << "Size is " << mainList.sendIntVector().size() << " " << mainList.sendDoubleVector().size() << std::endl;
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
                break;
            }case OP_TYPE::WORD_FREQ: {
                atomicNode wordTree('*'); //root
                while(!filesList.empty()){
                    threadVector.emplace_back(wordFreq, filesList.front().getFileName(), std::ref(wordTree));
                    filesList.pop();
                }
                print::Debug("Queue sucessfully created with size" + std::to_string(threadVector.size()));

                break;
            }case OP_TYPE::FIND_ALL: {
                while(!filesList.empty()){
                threadVector.emplace_back(findAll, filesList.front().getFileName(), LOOK_FOR_WORD);
                filesList.pop();
            }
            joinThreads(threadVector);
            break;
            }case OP_TYPE::FIND_ONE: {

            while(!filesList.empty()){
            threadVector.emplace_back(findOne, filesList.front().getFileName(), LOOK_FOR_WORD);
            filesList.pop();}
            joinThreads(threadVector);
            break;
            }
        print::Debug("Exiting assignOperation");
    }
    
}



void joinThreads(std::vector<std::thread>& threadVec){
    print::Debug("joinThreads() Entered");
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(const std::exception& e){
            std::string error = e.what();
            print::Debug(error);
        }
    }
    print::Debug("Threads have finished");
}
