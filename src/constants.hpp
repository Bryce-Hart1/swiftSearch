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

#include "var_type.hpp"

#include "bstd/bstd.hpp"
#include "bstd/store/log.hpp"
#include <iostream>
#include <print>
#include <string>
#include <mutex>
#include <unistd.h>



namespace parse{

enum class OPERATION_TYPE{
    UNSET,   //so a malformed command cannot dispatch on an indeterminate value
    INFO,
    SORTED_LIST,
    R_SORTED_LIST,
    LIST_NUMBERS,
    CHAR_FREQ,
    WORD_FREQ,
    FIND_ALL,

};


inline OPERATION_TYPE operationTypeOfParse = OPERATION_TYPE::UNSET; //global indicator for operation to be done on files
};

namespace print{

inline bstd::store::Logger swiftSearch_logbook{"swiftSearchLogs", true, true};
inline std::mutex printMutex; //the global mutex for all print functions to insure synced printing

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