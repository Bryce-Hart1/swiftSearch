/**
 * @brief holds debug state and all shared librarys
 * 
 * 
 */


#pragma once


#include<iostream>


enum debugState{
    DEBUG_ACTIVE,
    DEBUG_INACTIVE
};

enum OpertationType{
    

};



class customExceptions : public std::exception{
    private : std::string message;

    public : 
        customExceptions(const std::string& msg) : message(msg) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
};