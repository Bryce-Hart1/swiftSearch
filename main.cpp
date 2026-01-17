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



    cout << "Please input a file to parse in the following format" << endl;
    cout << "#sortedList @fileName" << endl;
    sleep(1);
    cout << "possible # operations: " << endl;
    cout << " #sortedList - takes all numbers and sorts them into a printable file, smallest to largest" << endl;
    cout << " #-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)" << endl;
    cout << "#listNumbers - takes all numbers and sorts them into a file in the order they appear" << endl;
    cout << "#listWords - takes all the words in the file and displays them in the order they appear." << endl;
    cout << "#findCharacterFrequency - finds the frequency of all values" << endl;
    cout << "findWordFreq - finds the frequency of all words in the list" << endl;
    sleep(1);
    cout << "If you would still like to contine, please enter this command now" << endl;

    
    

}

void checkArgLength(int argc, char *argv[], std::string &argumentArr){

    if(argc < 3){ //If length is invalid, the user gets a set number of tries before the program exits
        const int numberOfTries = 5;
        int currentCount = 0;
        std::string newStr;
        std::cout << "no command detected. please enter a valid command" << std::endl;
        printInfo(); //print list once before entering tries
        std::getline(std::cin, newStr); //if this is still invalid, we can clean it up later so not to have redundant token breakup
        argumentArr = newStr;
    }else{
        for(int i = 1; i < argc; ++i){
            argumentArr += std::string(argv[i]) + " ";
        }
    }

    if(DEBUG_ACTIVE){
        std::cout << "DEBUG ~~ checkArgLength exited with string being " << argumentArr << std::endl;
    }

}









int main(int argc, char *argv[]){
    DEBUG_ACTIVE = true; //hard set for now

    if(DEBUG_ACTIVE){
        std::cout << "DEBUG ~~ Debug mode is on, hello user." << std::endl;
    }


    std::string argumentArr;
    checkArgLength(argc, argv, argumentArr); //check intial Length
    


    const char userEntersCommand = 'c';
    const char userRequestsInfo = 'i';
    std::string rootname;
    std::string path;


    //fileTreeNode root(rootname);

    //Timer mainWatch;

    //mainWatch.start();
    //tokenMain(argumentArr); //first thing that needs done - make argumentArr into valid or invalid tokens - this will also check debug mode








    //mainWatch.stop();
    //std::cout << "Opertation has finished in " << mainWatch.timeInSeconds() << std::endl;



    if(DEBUG_ACTIVE){ 
        std::cout << "DEBUG ~~ Program exited successfully" << std::endl;
    }
}
