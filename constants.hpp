/**
 * @brief holds debug state and all shared librarys
 * 
 * 
 */


#pragma once


#include<iostream>
#include<thread>
#include<syncstream>
#include<sstream>

inline bool DEBUG_ACTIVE_FLAG;
inline bool NO_CAPITALS_FLAG;



enum class OP_TYPE{
    INFO,
    SORTED_LIST,
    R_SORTED_LIST,
    LIST_NUMBERS,
    LIST_WORDS,
    CHAR_FREQ,
    WORD_FREQ,
    FIND_ALL,
    FIND_ONE
};


inline std::mutex logThreadErrorMtx; //used just for function logThreadError in constants.hpp

//logs thread errors without interweaving. takes while exception as input
inline void logThreadError(const std::exception& e){
    std::lock_guard<std::mutex> lock(logThreadErrorMtx);
    std::cerr << e.what() << std::endl;
}


inline void printDebug(std::string message){
    if(DEBUG_ACTIVE_FLAG){
        std::string debugStatement = "DEBUG ~~ ";
        std::println("{} {}", debugStatement, message);

    }
}