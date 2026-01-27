#include"tokenize.hpp"
#include"constants.hpp"

#include<filesystem>
#include<vector>
#include<sstream>
#include<functional>
#include<queue>


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
        print::Debug(("Token One is invalid and value is " + tokenOne));
        return true;
    }

    return false; 
}


/**
 * -debug sets all debug messages to on
 *  -caps can only be called on char or string 
 *  -threadtime prints time that thread exits, along with file scanned
 *  for now implementation returns no signs that a flag failed to set, may change later
 */
void justifyFlags(std::vector<std::string> flagsDetected){
    print::Debug("justifyFlags() entered");
    std::array<std::string, 3> posFlag = {"-debug", "-caps", "-threadTime"}; 
    for(std::string flag : flagsDetected){
        if(posFlag[0] == flag){ //debug
            DEBUG_ACTIVE_FLAG = true;
            print::Debug("debug flag set.");
        }
        if(posFlag[1] == flag){
            NO_CAPITALS_FLAG = true;
            print::Debug("capitals flag set");
        }
        if(posFlag[2] == flag){
            SHOW_THREADTIME_FLAG = true;
            print::Debug("show threads set");
        }
    }
}

/**
 * @brief breaks down command line from main()
 * @details breaks down several parts:
 * 1st token: #Opertation tag followed by opertation
 * 2nd token: filepath 
 * 3rd token: debug mode and other flags
 * returns fileTreeStructure to main. sets flags and breaks down type of opertation to do
 * @param input string of tokens from user, unprocessed
 * @param root reference of root to use to create a fileTreeStructure
 * @returns a filetree structure if valid, and if not returns a nullptr
 */

fileTreeStructure* tokenize(std::string input){
    print::Debug("tokenize() entered");
    std::vector<std::string> tokens;
    const int maxTokenLimit = 10;
    std::string fileToLook;


    std::stringstream stream(input);
    std::string temp;
    while(stream >> temp){
        tokens.push_back(temp); //pushes all tokens into a vector
        std::string message = "tokens pushed back: values are: ";
        for(std::string t : tokens){
            message += t + " |";
        }
        print::Debug(message);
    }
    if(assignTokenOne(tokens.at(0))){
        print::Debug("Error assigning token one, exiting");
        std::exit(EXIT_FAILURE);
    }

    //sets some global booleans for tokens
    std::vector<std::string> flags;
    for(int i = 2; i < tokens.size(); i++){
        flags.push_back(tokens.at(i));
    }
    justifyFlags(flags); //sets global values here
    file root(tokens.at(1)); //root fileName should be found here

    
    try{
       fileTreeStructure* structure = new fileTreeStructure(root);
        return structure;

    }catch(std::exception e){
        std::cerr << e.what() << std::endl;
    }
    print::Debug("Object fileTreeStructure failed to create.");
    return nullptr;

}

