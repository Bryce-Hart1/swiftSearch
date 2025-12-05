#include<exception>
#include<string>
#include<fstream>
#include<string>
#include<iostream>
#include<sstream>
#include<thread>
#include<array>
#include<atomic>
#include<chrono>
#include<unistd.h>

#pragma once

class customExceptions : public std::exception{
    private : std::string message;

    public : 
        customExceptions(const std::string& msg) : message(msg) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
};



/**
 * 
 * 
 * 
 * 
 * @details adds nodes based a lock
 */


class wordBucket{


    public:
    //Node has count of times found, char* value 
    class node{
        private:
        char* value;
        std::atomic<int> counter;
    };

        private:
        std::atomic<int> depth;
        node root;


    wordBucket(){
        





        
    }




    void addNode(char* newWord){

    }






};









/**
 * @details does a O(1) opertation to add characters to buckets to print the count of them later
 * only will add ascii values to buckets (extended ascii: 0 - 255)
 * 
 */
class characterBucket{

    public:
        static const int MAXSIZE = 255; //size of array based off of max ASCII


    private:
    struct cPair{
        char value;
        unsigned int count;
    };


    public:
    

    private:
    std::array<cPair, MAXSIZE> list;



    //push a new count to the currentlist
    void push(char value){
        list[value].count++;
    }
    //return value at a character value
    unsigned int getCount(char value){
        return list[value].count;
    }

    void printPair(char value){
        std::cout<< list[value].value << " " << list[value].count;
    }

};