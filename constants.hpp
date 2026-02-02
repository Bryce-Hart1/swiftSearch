/**
 ____          _  __ _   ____                      _     
/ ___|_      _(_)/ _| |_/ ___|  ___  __ _ _ __ ___| |__  
\___ \ \ /\ / / | |_| __\___ \ / _ \/ _` | '__/ __| '_ \ 
 ___) \ V  V /| |  _| |_ ___) |  __/ (_| | | | (__| | | |
|____/ \_/\_/ |_|_|  \__|____/ \___|\__,_|_|  \___|_| |_|

 * Welcome to swiftSearch.
 * @author Bryce Hart
 * @date February 1st, 2026
 * @brief This program allows for fast search of multiple files using multiple threads.
 */
#pragma once


#include<iostream>
#include<thread>
#include<syncstream>
#include<sstream>

inline bool DEBUG_ACTIVE_FLAG = false;
inline bool NO_CAPITALS_FLAG = false;
inline bool SHOW_THREADTIME_FLAG = false;
inline bool FLOAT_NUMBER_LIST_FLAG = false;


enum class OP_TYPE{
    INFO,
    SORTED_LIST,
    R_SORTED_LIST,
    LIST_NUMBERS,
    CHAR_FREQ,
    WORD_FREQ,
    FIND_ALL,
};

inline OP_TYPE operationTypeOfParse; //global indicator for operatation to be done on files
inline std::mutex printMutex; //the global mutex for all print functions to insure synced printing

inline std::string LOOK_FOR_WORD;

//converts string passed to lowercase
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

//checks if string provided has a number inside
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

inline void Error(const std::exception& e){
    std::lock_guard<std::mutex> lock(printMutex);
    std::cerr << "[EXCEPTION] :: " <<  e.what() << std::endl;
}

inline void Thread(std::string message, std::string threadsFile){
    if(SHOW_THREADTIME_FLAG){
    std::lock_guard<std::mutex> lock(printMutex); //uses same mutex as logThread error, just in case
    std::string threadStatement = ("[THREAD] [" + threadsFile + "]~~ ");
    std::println(std::cout, "{} {} ", threadStatement, message);
    }
}

inline void Found(std::string keyWord, std::string fileName,int instance, int line){
    std::println(std::cout, "! -> instance |{}| of |keyWord| found at line |{}| : |{}|", instance, keyWord, line, fileName);
}
inline void User(std::string msg){
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

}

namespace str{ //simple for now, may include times later
    inline std::string enter = "Starting task ... ";
    inline std::string exit = "Exiting task ... ";
}