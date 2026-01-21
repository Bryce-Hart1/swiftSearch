/**
 * @brief holds debug state and all shared librarys
 * 
 * 
 */


#pragma once


#include<iostream>


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



class customExceptions : public std::exception{
    private : std::string message;

    public : 
        customExceptions(const std::string& msg) : message(msg) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
};


void printDebug(std::string message){
    if(DEBUG_ACTIVE_FLAG){
        std::string debugStatement = "DEBUG ~~ ";
        std::println("{} {}", debugStatement, message);

    }
}