#include"parse.hpp"
#include"tokenize.hpp"
#include"classes.hpp"
#include"constants.hpp"

#include<unistd.h>



void printInfo(){
    using namespace std;
    println(cout,"Please input a file to parse in the following format");
    println(cout,"#sortedList user/cool/file/directory -flag");
    sleep(1);
    println(cout,"possible # operations: (Start with a #): ");
    sleep(1);
    println(cout, "#findAll - returns all the times the instance is found");
    println(cout,"#sortedList - takes all numbers and sorts them into a printable file, smallest to largest");
    println(cout,"#-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)");
    println(cout,"#listNumbers - takes all numbers and sorts them into a file in the order they appear");
    println(cout,"#listWords - takes all the words in the file and displays them in the order they appear.");
    println(cout,"#getCharacter - finds the frequency of all values");
    println(cout,"getWords - finds the frequency of all words in the list");
    sleep(1);
    println(cout,"possible flags (start with a - ):");
    sleep(1);
    println(cout,"-threadinfo (prints when file enter/exits)");
    println(cout, "-floatlist will store numbers with a decimal place");
    println(cout,"-debug prints debug messages for important runtime events");
    println(cout,"-caps will not save capital letters, and searches will be done on the files converted to lowercase");
    println(cout,"If you would still like to contine, please enter this command now");
}


void checkArgLength(int argc, char *argv[], std::string &argumentArr){

    if(argc < 3){ //If length is invalid, the user gets a set number of tries before the program exits
        std::string newStr;
        std::println(std::cout,"No command detected.");
        printInfo(); //print list once before entering tries
        std::getline(std::cin, newStr); //if this is still invalid, we can clean it up later so not to have redundant token breakup
        argumentArr = newStr; //if they fail again just exit.
    }else{
        for(int i = 1; i < argc; ++i){
            argumentArr += std::string(argv[i]) + " ";
        }
    }
    std::string debug = ("ArgArr is " + argumentArr);
    print::Debug(debug);

}


int main(int argc, char *argv[]){
    print::Logo();
    std::string argumentArr;
    checkArgLength(argc, argv, argumentArr); //check intial Length if length is good, continue
    file root; 
    Timer mainWatch;
    mainWatch.start(); //start timer
    fileTreeStructure fileStruct = *tokenize(argumentArr, std::ref(mainWatch));
    print::Debug("Debug mode is on");

    assignOperation(operationTypeOfParse, fileStruct.createFileQueue());
    
    mainWatch.stop();
    std::println(std::cout, "Opertation has finished in {}", mainWatch.readableTime());


    print::Debug("Program sucessfully exited");
}
