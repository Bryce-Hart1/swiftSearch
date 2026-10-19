#include "classes.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>
#include <exception>
#include <stdexcept>

using fsError = std::filesystem::filesystem_error;

/**
 * @details 
 * @param f this is the file that is pulled from the queue for this thread to work on
 * @param keyWord will return this count of this string to the shared memory, which is just a shared count
 */
void findAll(file f, std::string keyWord){
    using namespace std;

    print::Thread(str::enter, f.getFileName());

    if(keyWord.empty()){ //parseWindow(0) would pop_front an empty deque
        print::Debug("Empty search term, nothing to look for in " + f.getFileName());
        print::Thread(str::exit, f.getFileName());
        return;
    }

    simpleCount counter;

    char ch; //takes whats coming in character by character
    parseWindow Window(keyWord.size());
    //rowOffile used to be initialised to 0 and never incremented, so every hit
    //reported line 0. lines are 1-based; the counter tracks the line the match
    //ENDS on, since the window only completes keyWord.size() bytes in.
    unsigned long long rowOffile = 1;
    try{
    ifstream inputFile(f.filePathToStr());
    inputFile >> noskipws; //reads everything w spaces
        if(inputFile.is_open()){
            while(inputFile >> ch){
                Window.move(ch);
                if(Window.isCorrectSequence(keyWord)){
                    counter.incrementBy(1); //increment first: instances are 1-based
                    print::Found(keyWord, f.getFileName(), counter.getCount(), rowOffile);
                }
                if(ch == '\n'){
                    rowOffile++;
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
    



void wordFreq(file f, threadsafe::Trie& wordTree){
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

/**
 * @brief works with word freq/trie to print to file 
 */
void printToFile(const std::vector<std::string>& words){
    using namespace std;
    ofstream outfile;
    outfile.open("Words.txt");
    if(!outfile.is_open()){
        print::Error(runtime_error("Words.txt failed to open")); 
    }
    size_t current_wordsCount = 0;
    string wordToPrint;
    for(size_t i = 0; i < words.size(); i++){
        if(i == 0){
            wordToPrint = words.at(i);
            current_wordsCount++;
        }else{
            if(words.at(i-1) == words.at(i)){ //another instance, just continue after incrementing
                current_wordsCount++;
            }else{ //print word and count at i-1 and overwrite
                outfile << current_wordsCount << ": " << wordToPrint << "\n";
                current_wordsCount = 1;
                wordToPrint = words.at(i);
            }
        }
    }
    //the last group was still buffered here and never written, so the
    //alphabetically last word never made it into the file
    if(!words.empty()){
        outfile << current_wordsCount << ": " << wordToPrint << "\n";
    }
    outfile.close();
    print::User("All words are saved in Words.txt"); 

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
                debugCounter++;
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
 * @fn assignOperation 
 * @brief does most of the main operation work at runtime, including assigning operations through the switch,
 * doing the related work with that operation, and eventually returning after using @fn joinThreads if needed
 * @param operation previously set operation type 
 * @param filesList queue of files to run through
 */

void assignOperation(OP_TYPE operation, std::queue<file> filesList){
    std::vector<std::thread> threadVector;

    if(filesList.empty()){
        print::User("No files to scan.");
        return;
    }

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
                print::User("Finished list is in ReverseSortedList.txt");
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
                joinThreads(threadVector); //separate join function
                bucketArr.printAll(); 
                break;
            }
            case OP_TYPE::WORD_FREQ: {
                const size_t CUTOFF = 5;
                threadsafe::Trie wordTree(CUTOFF);

                while(!filesList.empty()){
                    threadVector.emplace_back(wordFreq, filesList.front(), std::ref(wordTree));
                    filesList.pop();
                }
                joinThreads(threadVector);//send all to work
                std::vector<std::string> allWords = wordTree.getWords(); //then retrieve
                print::Debug(std::to_string(allWords.size()) + " Words saved from threads");
                printToFile(allWords); //then add to file
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
            case OP_TYPE::UNSET:
            default: {
                print::Error(std::runtime_error("No operation was set"));
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
