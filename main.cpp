#include"parse.hpp"
#include"tokenize.hpp"
#include"classes.hpp"
#include"constants.hpp"

#include<unistd.h>



void printInfo(){
    using namespace std;
    println(cout,"Please input a file to parse in the following format");
    println(cout,"#sortedList @fileName -flag");
    sleep(1);
    println(cout,"possible # operations: ");
    sleep(1);
    println(cout," #sortedList - takes all numbers and sorts them into a printable file, smallest to largest");
    println(cout," #-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)");
    println(cout,"#listNumbers - takes all numbers and sorts them into a file in the order they appear");
    println(cout,"#listWords - takes all the words in the file and displays them in the order they appear.");
    println(cout,"#findCharacterFrequency - finds the frequency of all values");
    println(cout,"findWordFreq - finds the frequency of all words in the list");
    sleep(1);
    println(cout,"If you would still like to contine, please enter this command now");
}

void checkArgLength(int argc, char *argv[], std::string &argumentArr){

    if(argc < 3){ //If length is invalid, the user gets a set number of tries before the program exits
        const int numberOfTries = 5;
        int currentCount = 0;
        std::string newStr;
        std::println(std::cout,"No command detected.");
        printInfo(); //print list once before entering tries
        std::getline(std::cin, newStr); //if this is still invalid, we can clean it up later so not to have redundant token breakup
        argumentArr = newStr;
    }else{
        for(int i = 1; i < argc; ++i){
            argumentArr += std::string(argv[i]) + " ";
        }
    }
    std::string debug = ("ArgArr is " + argumentArr);
    print::Debug(debug);

}









int main(int argc, char *argv[]){
    DEBUG_ACTIVE_FLAG = true; //hard set for now



    std::string argumentArr;
    checkArgLength(argc, argv, argumentArr); //check intial Length if length is good, continue
    file root; 
    Timer mainWatch;
    mainWatch.start(); //start timer
    fileTreeStructure fileStruct = *tokenize(argumentArr);
    print::Debug("Debug mode is on");
    std::vector<std::thread> threadContainer;
    assignOperation(threadContainer, operationTypeOfParse, fileStruct.createStringQueue());
    mainWatch.stop();
    std::println(std::cout, "Opertation has finished in {}", mainWatch.readableTime());


    print::Debug("Program sucessfully exited");
}
