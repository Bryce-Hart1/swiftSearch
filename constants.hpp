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
inline bool FLOAT_NUMBER_LIST_FLAG;


enum class OP_TYPE{
    INFO,
    SORTED_LIST,
    R_SORTED_LIST,
    LIST_NUMBERS,
    CHAR_FREQ,
    WORD_FREQ,
    FIND_ALL,
    FIND_ONE
};

inline OP_TYPE operationTypeOfParse; //global indicator for operatation to be done on files
inline std::mutex printMutex; //the global mutex for all print functions to insure synced printing

inline std::string LOOK_FOR_WORD;


/**
 * @param str string to convert to lowercase
 */
inline std::string toLowerCase(std::string str){
    std::string returnStr;
    for(size_t i = 0; i < str.length(); i++){
        if(str[i] >= 'A' && str[i] <= 'Z'){
            returnStr += (str[i] + 32);
        }else{
            returnStr += str[i];
        }
    }
    return returnStr;
}

inline bool isNumber(const std::string& word){
    if(word.empty()){
        return false;
    }
    if(word.at(0) >= '0' && word.at(0) <= '9'){
        return true;
    }
    for(size_t i = 0; i < word.length(); i++){ //might remove/optimize later
        if(word.at(i) >= '0' && word.at(i) <= '9'){
        return true; }    
    }
    return false;
}




namespace print{

inline void Debug(std::string message){
    if(DEBUG_ACTIVE_FLAG){
        std::lock_guard<std::mutex> lock(printMutex);
        std::string debugStatement = "[DEBUG] || ";
        std::println(std::cout, "{} {}", debugStatement, message);

    }
}

//logs thread errors without interweaving. takes while exception as input
inline void Error(const std::exception& e){
    std::lock_guard<std::mutex> lock(printMutex);
    std::cerr << "[EXCEPTION] :: " <<  e.what() << std::endl;
}

inline void Thread(std::string message, std::string threadsFile){
    std::lock_guard<std::mutex> lock(printMutex); //uses same mutex as logThread error, just in case
    std::string threadStatement = ("[THREAD] [" + threadsFile + "]~~ ");
    std::println(std::cout, "{} {} ", threadStatement, message);
}

inline void Found(std::string keyWord, std::string fileName,int instance, int line){
    std::println(std::cout, "! -> instance |{}| of |keyWord| found at line |{}| : |{}|", instance, keyWord, line, fileName);
}
inline void User(std::string msg){
    std::println(std::cout, "[USER] {}", msg);
}


}

namespace str{ //simple for now, may include times later
    inline std::string enter = "Starting task ... ";
    inline std::string exit = "Exiting task ... ";
}