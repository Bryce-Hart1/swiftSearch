/**
 ____          _  __ _   ____                      _     
/ ___|_      _(_)/ _| |_/ ___|  ___  __ _ _ __ ___| |__  
\___ \ \ /\ / / | |_| __\___ \ / _ \/ _` | '__/ __| '_ \ 
 ___) \ V  V /| |  _| |_ ___) |  __/ (_| | | | (__| | | |
|____/ \_/\_/ |_|_|  \__|____/ \___|\__,_|_|  \___|_| |_|

 * Welcome to swiftSearch.
 * @author Bryce Hart
 * @date February 1st, 2026, updated (V1.1 on 3/12/26)
 * @brief This program allows for fast search of multiple files using multiple threads.
 */
#pragma once


#include <iostream>
#include <thread>
#include <syncstream>
#include <sstream>
#include <print>
#include <string>
#include <mutex>
#include <unistd.h>

#include "tsTrie.hpp" //my own trie implementation



inline bool DEBUG_ACTIVE_FLAG = false;
inline bool NO_CAPITALS_FLAG = false;
inline bool SHOW_THREADTIME_FLAG = false;
inline bool FLOAT_NUMBER_LIST_FLAG = false;


enum class OP_TYPE{
    UNSET,   //so a malformed command cannot dispatch on an indeterminate value
    INFO,
    SORTED_LIST,
    R_SORTED_LIST,
    LIST_NUMBERS,
    CHAR_FREQ,
    WORD_FREQ,
    FIND_ALL,
};

inline OP_TYPE operationTypeOfParse = OP_TYPE::UNSET; //global indicator for operatation to be done on files
inline std::mutex printMutex; //the global mutex for all print functions to insure synced printing

inline std::string LOOK_FOR_WORD;

//converts string passed to lowercase
inline std::string toLowerCase(std::string str){
    for(char& c : str){
        if(c >= 'A' && c <= 'Z'){
            c = static_cast<char>(c + 32);
        }
    }
    return str;
}

//checks if string provided has a number inside
inline bool isNumber(const std::string& word){
    for(char c : word){
        if(c >= '0' && c <= '9'){
            return true;
        }
    }
    return false;
}




namespace print{

inline void Debug(const std::string& message){
    if(DEBUG_ACTIVE_FLAG){
        std::lock_guard<std::mutex> lock(printMutex);
        std::string debugStatement = "[DEBUG] || ";
        std::println(std::cout, "{} {}", debugStatement, message);
    }
}

inline void Error(const std::exception& e){
    std::lock_guard<std::mutex> lock(printMutex);
    std::cerr << "[EXCEPTION] :: " <<  e.what() << std::endl;
}

inline void Thread(const std::string& message, const std::string& threadsFile){
    if(SHOW_THREADTIME_FLAG){
    std::lock_guard<std::mutex> lock(printMutex); //uses same mutex as logThread error, just in case
    std::string threadStatement = ("[THREAD] [" + threadsFile + "]~~ ");
    std::println(std::cout, "{} {} ", threadStatement, message);
    }
}

inline void Found(const std::string& keyWord, const std::string& fileName,
                  unsigned long long instance, unsigned long long line){
    //this format string used to have three {} for four arguments, with "|keyWord|"
    //as literal text. keyWord printed in the line slot, line printed in the file
    //slot, and fileName never printed at all.
    std::lock_guard<std::mutex> lock(printMutex);
    std::println(std::cout, "! -> instance |{}| of |{}| found at line |{}| : |{}|",
                 instance, keyWord, line, fileName);
}
inline void User(const std::string& msg){
    std::lock_guard<std::mutex> lock(printMutex);
    std::println(std::cout, "[USER] {}", msg);
}

inline void Logo(){
    using namespace std;
    println(cout,R"( ____          _  __ _   ____                      _     )");
    println(cout,R"(/ ___|_      _(_)/ _| |_/ ___|  ___  __ _ _ __ ___| |__  )");
    println(cout,R"(\___ \ \ /\ / / | |_| __\___ \ / _ \/ _` | '__/ __| '_ \ )");
    println(cout,R"( ___) \ V  V /| |  _| |_ ___) |  __/ (_| | | | (__| | | |)");
    println(cout,R"(|____/ \_/\_/ |_|_|  \__|____/ \___|\__,_|_|  \___|_| |_|)");
    println(cout,"=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=");
}

inline void Info(){
    using namespace std;
    println(cout,"Please input a file to parse in the following format");
    println(cout,"#sortedList user/cool/file/directory -flag");
    sleep(1);
    println(cout,"possible # operations: (Start with a #): ");
    sleep(1);
    println(cout, "#findAll - returns all the times the instance is found");
    println(cout,"#sortedList - takes all numbers and sorts them into a printable file, smallest to largest");
    println(cout,"#-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)");
    println(cout,"#listNumbers - takes all numbers and sorts them into a file in the order they appear");
    println(cout,"#getCharacter - finds the frequency of all values");
    println(cout,"getWords - finds the frequency of all words in the list");
    sleep(1);
    println(cout,"possible flags (start with a - ):");
    sleep(1);
    println(cout,"-threadinfo (prints when file enter/exits)");
    println(cout, "-floatlist will store numbers with a decimal place");
    println(cout,"-debug prints debug messages for important runtime events");
    println(cout,"-caps will not save capital letters, and searches will be done on the files converted to lowercase");
    println(cout,"If you would still like to contine, please enter this command now");
}



}

namespace str{ //simple for now, may include times later
    inline std::string enter = "Starting task ... ";
    inline std::string exit = "Exiting task ... ";
}