#include"tokenize.hpp"
#include"constants.hpp"

#include<mutex>
#include<filesystem>
#include<vector>
#include<sstream>
#include<algorithm> 
#include<functional>
#include<queue>
#include<cstdlib>



//Node can function independant of fileTree, but we cannot start a new fileTree without a root
//I also need the tokenizer to catch the missing file name BEFORE we hit the stage where we set up a tree.

/**
 * @param str string to convert to lowercase
 */
std::string toLowerCase(std::string str){
    std::string returnStr;
    for(int i = 0; i < str.length(); i++){
        if(str[i] >= 'A' && str[i] <= 'Z'){
            returnStr += (str[i] - 32);
        }else{
            returnStr += str[i];
        }
    }
    return returnStr;
}


//Will throw true if there is an error assigning token. If this occurs, program will exit.
bool assignTokenOne(std::string tokenOne){
    std::array<std::string, 7> Tokens = {"#info", "#sortedlist", "#-sortedlist", 
    "#listnumbers", "#listwords", "#findcharacterfrequency", "findwordfreq"};
    tokenOne = toLowerCase(tokenOne);
    int goThoughTokens = 0;
    int foundAt = -1;
    for(int i = 0; i < Tokens.size(); i++){
        if(Tokens.at(i) == tokenOne){
            foundAt = i;
        }
    }
    if(foundAt == -1){
        std::println("Operation is not valid. Make sure it starts with # and is one word.");
        printDebug(("Token One is invalid and value is " + tokenOne));
    }

    return false; //not yet finished
}

bool assignTokenTwo(std::string root){
    
    return false; //not emplemented
}


void justifyFlags(std::vector<std::string> flagsDetected){

}

/**
 * @brief breaks down command line from main()
 * 
 * @details breaks down several parts:
 * 1st token: #Opertation tag followed by opertation
 * 2nd token: filepath 
 * 3rd token: debug mode and other flags
 */


void tokenMain(std::string input, file &root){
    std::vector<std::string> tokens;
    const int maxTokenLimit = 10;
    std::string fileToLook;


    std::stringstream stream(input);
    std::string temp;
    while(stream >> temp){
        tokens.push_back(temp); //pushes all tokens into a vector
    }
    if(assignTokenOne(tokens.at(0))){
        printDebug("Error assigning token one, exiting");
        std::exit(EXIT_FAILURE);
    }
    if(assignTokenTwo(tokens.at(1))){
        printDebug("Error assigning token two (fileTreeStructure) exiting");
        std::exit(EXIT_FAILURE);
    }
    


    


}

