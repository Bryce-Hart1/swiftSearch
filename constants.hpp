/**
 * @brief holds Enum of operation type, and possible flags and print functions
 */


#pragma once


#include<iostream>
#include<thread>
#include<syncstream>
#include<sstream>

inline bool DEBUG_ACTIVE_FLAG;
inline bool NO_CAPITALS_FLAG;
inline bool SHOW_THREADTIME_FLAG;


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

inline OP_TYPE operationTypeOfParse; //global indicator for operatation to be done on files
inline std::mutex printMutex; //the global mutex for all print functions to insure synced printing


namespace print{

inline void Debug(std::string message){
    if(DEBUG_ACTIVE_FLAG){
        std::lock_guard<std::mutex> lock(printMutex);
        std::string debugStatement = "DEBUG || ";
        std::println(std::cout, "{} {}", debugStatement, message);

    }
}

//logs thread errors without interweaving. takes while exception as input
inline void Error(const std::exception& e){
    std::lock_guard<std::mutex> lock(printMutex);
    std::cerr << "EXCEPTION :: " <<  e.what() << std::endl;
}

inline void Thread(std::string message, int threadNumber){
    std::lock_guard<std::mutex> lock(printMutex); //uses same mutex as logThread error, just in case
    std::string threadStatement = ("THREAD [" + std::to_string(threadNumber)) + "]~~ ";
    std::println(std::cout, "{} {} ", threadStatement, message);
}

}