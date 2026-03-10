#include "parse.hpp"
#include "constants.hpp"
#include "classes.hpp"
#include "tsTrie.hpp"

#include<filesystem>
#include<vector>
#include<iostream>
#include<string>
#include<fstream>
#include<exception>
#include<thread>


using fsError = std::filesystem::filesystem_error;

/**
 * @details 
 * @param f this is the file that is pulled from the queue for this thread to work on
 * @param keyWord will return this count of this string to the shared memory, which is just a shared count
 */
void findAll(file f, std::string keyWord){
    using namespace std;

    print::Thread(str::enter, f.getFileName());
    simpleCount counter;

    char ch; //takes whats coming in character by character
    parseWindow Window(keyWord.size());
    int rowOffile = 0;
    try{
    ifstream inputFile(f.filePathToStr());
    inputFile >> std::noskipws; //reads everything w spaces
        if(inputFile.is_open()){
            while(inputFile >> ch){
                Window.move(ch);
                if(Window.isCorrectSequence(keyWord)){
                    print::Found(keyWord, f.getFileName(), counter.getCount(), rowOffile);
                    counter.incrementBy(1);
                }
            }
        }else{
        print::Debug("File " + f.getFileName() + " did not open");
        }


    }catch(const fsError& e){
        print::Error(e);
    }
    print::Thread(str::exit, f.getFileName());
}
    



void wordFeq(file f, threadsafe::Trie wordTree){
    print::Thread(str::enter, f.getFileName());
    std::string word;
    try{
        std::ifstream inputFile(f.filePathToStr());
        if(inputFile.is_open()){
            while(inputFile >> word){
                wordTree.add(word);
            }
        }else{
            print::Debug("File " + f.getFileName() + " did not open");
        }
    }catch(const fsError& e){
        print::Error(e);
    }
    print::Thread(str::exit, f.getFileName());
}


void printToFile(std::vector<std::string> words){

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
 * works with both vectors in numberList, both double and int
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
                print::User("Finshed list is in ReverseSortedList.txt");
                break;
            }
            case OP_TYPE::SORTED_LIST: {
                numberList mainList = numberListHelper::combinedList(filesList);
                mainList.sort();
                mainList.printToFile("SortedList.txt");
                print::User("Finished list is in Sorted.txt");
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
                const std::size_t CUTOFF = 5;
                threadsafe::Trie wordTree(CUTOFF);

                while(!filesList.empty()){
                    threadVector.emplace_back(wordFreq, filesList.front(), std::ref(wordTree));
                    filesList.pop();
                }
                joinThreads(threadVector);//send all to work
                std::vector<std::string> allWords = wordTree.getWords(); //then retrieve
                print::Debug(std::to_string(allWords.size()) + "Words saved");
                //printToFile(); //implement
                print::User("All words are saved in Words.txt"); 
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
            print::Debug("joinThreads() has failed");
        }
    }
}
