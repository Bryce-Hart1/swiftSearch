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
void findAll(file f, std::string keyWord){
    print::Thread(str::enter, f.getFileName());
    using namespace std;
    int rowOffile = 0;
    simpleCount counter;

    string line;

    try{
    ifstream inputFile(f.filePathToStr());
        if(inputFile.is_open()){
            while(getline(inputFile, line)){
                istringstream strstream(line);
                string word;
                rowOffile++;
            while(strstream >> word){
                if(keyWord == word){
                    counter.incrementBy(1);
                    print::Found(keyWord, f.getFileName(), counter.getCount(), rowOffile);
                }

            }
        }
        }else{
        print::Debug("File " + f.getFileName() + " did not open");
        }


    }catch(const std::filesystem::filesystem_error& e){
        print::Error(e);
    }
    print::Thread(str::exit, f.getFileName());
}
    



/**
 * @fn takes a passed in atomicNode and adds words to it, returning when finished
 * @param f the file being passed in
 * @param wordTree the shared atomicNode Tree
 */
void wordFreq(file f, atomicNode &wordTree){
    print::Thread(str::enter, f.getFileName());
    using namespace std;
    size_t count = 0;
    try{
        ifstream input(f.filePathToStr());
        string word;

        while(input >> word){
            if(!isNumber(word)){
            wordTree.add(word);
            count++;
            }
        }
    }catch(const exception& e){
        string error = e.what();
        print::Debug(error);
    }
    print::Thread(to_string(count) + " words were added to tree.", f.getFileName());
    print::Thread(str::exit, f.getFileName());
}


/**
 * @fn charFreq 
 * @brief using charbucket to find character frequency.
 * puts all characters found in the file into a combined charbucket
 * As of right now, it does not track spaces but this is by design.
 */
void charFreq(file f, characterBucket &cBucket){
    print::Thread(str::enter, f.getFileName());
    size_t debugCounter = 0;
    try{
        std::ifstream input(f.filePathToStr());
        std::string word;

        while(input >> word){
            for(char letter : word){
                cBucket.addTo(letter);

            }
        }
    }catch(const std::exception &e){
        print::Error(e);
    }
    print::Debug(std::to_string(debugCounter) + " Chars found in " + f.getFileName());
    print::Thread(str::exit, f.getFileName());
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
            }
            case OP_TYPE::LIST_NUMBERS: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.printToFile("ListedNumbers.txt");
                break;
            }
            case OP_TYPE::R_SORTED_LIST: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.sort();
                mainList.reverse();
                mainList.printToFile("ReverseSortedList.txt");
                break;
            }
            case OP_TYPE::SORTED_LIST: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.sort();
                mainList.printToFile("SortedList.txt");
                std::cout << "Size is " << mainList.sendIntVector().size() << " " << mainList.sendDoubleVector().size() << std::endl;
                break;
            }
            case OP_TYPE::CHAR_FREQ: {
                characterBucket bucketArr;
                while(!filesList.empty()){
                    threadVector.emplace_back(charFreq, filesList.front(), std::ref(bucketArr));
                    filesList.pop();
                }
                joinThreads(threadVector); //seperate join function
                bucketArr.printAll(); 
                break;
            }
            case OP_TYPE::WORD_FREQ: {
                atomicNode wordTree('*'); //root
                while(!filesList.empty()){
                    threadVector.emplace_back(wordFreq, filesList.front(), std::ref(wordTree));
                    filesList.pop();
                }
                joinThreads(threadVector);
                std::vector<std::string> wordVec;
                print::Debug("Root child count: " + std::to_string(wordTree.getChildCount()));
                wordTree.saveSet("", wordVec);
                print::Debug(std::to_string(wordVec.size()) + "Words saved");
                print::User("All words are saved in Words.txt"); 
                wordTree.printTofile("Words.txt", wordVec);  
                break;
            }
            case OP_TYPE::FIND_ALL: {
                while(!filesList.empty()){
                threadVector.emplace_back(findAll, filesList.front(), LOOK_FOR_WORD);
                filesList.pop();
            }
            joinThreads(threadVector);
            break;
            }
    }
    
}



void joinThreads(std::vector<std::thread>& threadVec){
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(const std::exception& e){
            print::Error(e);
        }
    }
    print::Debug("Threads have finished");
}
