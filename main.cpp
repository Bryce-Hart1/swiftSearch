#include"parse.hpp"
#include"tokenize.hpp"
#include"classes.hpp"

#include<iostream>
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





bool debugModeIsOn = false; //this will get reset in tokenizer, but is off by default 


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

}











int main(int argc, char *argv[]){
    std::string argumentArr = argv[1]; //first letter of the string pass this to tokenMain for breakdown

    const char userEntersCommand = 'c';
    const char userRequestsInfo = 'i';
    std::string rootname;
    std::string path;


    fileTreeNode root(rootname);

    tokenMain(argumentArr); //first thing that needs done - make argumentArr into valid or invalid tokens - this will also check debug mode
    if(debugModeIsOn){
        std::cout << "Debug mode is on" << std::endl;
    }



    //if user does not understand to enter on commandLine, we need then to have a valid command
        std::cout << "Enter command: " << userEntersCommand << " View Formatting " << userRequestsInfo;



}