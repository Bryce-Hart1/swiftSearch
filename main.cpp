#include"parse.hpp"
#include"tokenize.hpp"
#include"classes.hpp"
#include"constants.hpp"

#include<unistd.h>

/*
Possible operations:
#d collect instances of term
#df print every instance of term
#createList builds a file of all terms found in the files
three options 
default sort (no token passed first of trie)
> greatest amount to least amount 
< least amount to greatest amount
#createChar builds a file that shows amount of characters found in the files 




format of command -
#operation fileToparse



*/







/**
 * @details added this to give users a way to get used to the program and learn commands
 * 
 */
void printInfo(){
    using namespace std;


    println("Please input a file to parse in the following format");
    println("#sortedList @fileName -flag");
    sleep(1);
    println("possible # operations: ");
    sleep(1);
    println(" #sortedList - takes all numbers and sorts them into a printable file, smallest to largest");
    println(" #-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)");
    println("#listNumbers - takes all numbers and sorts them into a file in the order they appear");
    println("#listWords - takes all the words in the file and displays them in the order they appear.");
    println("#findCharacterFrequency - finds the frequency of all values");
    println("findWordFreq - finds the frequency of all words in the list");
    sleep(1);
    println("If you would still like to contine, please enter this command now");

    
    

}

void checkArgLength(int argc, char *argv[], std::string &argumentArr){

    if(argc < 3){ //If length is invalid, the user gets a set number of tries before the program exits
        const int numberOfTries = 5;
        int currentCount = 0;
        std::string newStr;
        std::println("No command detected.");
        printInfo(); //print list once before entering tries
        std::getline(std::cin, newStr); //if this is still invalid, we can clean it up later so not to have redundant token breakup
        argumentArr = newStr;
    }else{
        for(int i = 1; i < argc; ++i){
            argumentArr += std::string(argv[i]) + " ";
        }
    }
    std::string debug = ("ArgArr is " + argumentArr);
    printDebug(debug);

}









int main(int argc, char *argv[]){
    DEBUG_ACTIVE_FLAG = true; //hard set for now



    std::string argumentArr;
    checkArgLength(argc, argv, argumentArr); //check intial Length if length is good, continue
    file root();
    //tokenMain(argumentArr, root); 
    printDebug("Debug mode is on");
    Timer mainWatch;
    mainWatch.start(); //start timer

    //set a enum of the operation, with the root already set.
    //fileTreeNode root(rootname); make tree
    //join threads
    //wait for threads to finish
    mainWatch.stop();
    std::println("Opertation has finished in {}", mainWatch.readableTime());


    printDebug("Program sucessfully exited");
}
