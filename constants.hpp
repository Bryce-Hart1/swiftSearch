/**
 * @brief holds debug state and all shared librarys
 * 
 * 
 */


#pragma once


#include<iostream>


bool DEBUG_ACTIVE;



enum class OpertationType{
    

};



class customExceptions : public std::exception{
    private : std::string message;

    public : 
        customExceptions(const std::string& msg) : message(msg) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
};
