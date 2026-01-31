#include"tokenize.hpp"
#include"constants.hpp"

#include<filesystem>
#include<vector>
#include<sstream>
#include<functional>
#include<queue>


//Will throw true if there is an error assigning token. If this occurs, program will exit from tokenize
bool assignTokenOne(std::string tokenOne){
    std::array<std::string, 8> Tokens = {"#info", "#sortedlist", "#-sortedlist", 
    "#listnumbers", "#findcharacterfrequency", "#findwordfreq", "#findone", "#findall"};
    tokenOne = toLowerCase(tokenOne);
    std::cout << tokenOne << "\n";
    int foundAt = -1;
    for(size_t i = 0; i < Tokens.size(); i++){
        if(Tokens.at(i) == tokenOne){
            foundAt = i;
        }
    }
    print::Debug("Entering token one assignment...");

    switch(foundAt){ //set op type
        case 0: operationTypeOfParse = OP_TYPE::INFO;
        break;
        case 1: operationTypeOfParse = OP_TYPE::SORTED_LIST;
        break;
        case 2: operationTypeOfParse = OP_TYPE::R_SORTED_LIST;
        break;
        case 3: operationTypeOfParse = OP_TYPE::LIST_NUMBERS;
        break;
        case 4: operationTypeOfParse = OP_TYPE::CHAR_FREQ;
        break;
        case 5: operationTypeOfParse = OP_TYPE::WORD_FREQ;
        break;
        case 6: operationTypeOfParse = OP_TYPE::FIND_ONE;
        break;
        case 7: operationTypeOfParse = OP_TYPE::FIND_ALL;
        break;
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
 *  -find finds word.
 *  for now implementation returns no signs that a flag failed to set, may change later
 */
void justifyFlags(std::vector<std::string> flagsDetected){

    print::Debug("justifyFlags() entered");
    std::array<std::string, 4> posFlag = {"-debug", "-caps", "-threadtime", "-floatlist"}; 
    for(std::string flag : flagsDetected){
        if(posFlag[0] == flag){ //debug
            DEBUG_ACTIVE_FLAG = true;
            print::Debug("debug flag set.");
        }
        if(posFlag[1] == flag){ //capitals
            NO_CAPITALS_FLAG = true;
            print::Debug("capitals flag set");
        }
        if(posFlag[2] == flag){
            SHOW_THREADTIME_FLAG = true;
            print::Debug("show threads flag set");
        }
        if(posFlag[3]== flag){
            FLOAT_NUMBER_LIST_FLAG = true;
            print::Debug("Float numbers flag set");
        }
    }
    if(operationTypeOfParse == OP_TYPE::FIND_ALL || operationTypeOfParse == OP_TYPE::FIND_ONE){
        print::Debug("Operation type is: search");
        std::println(std::cout, "Please enter the word you are looking for");
        std::cin >> LOOK_FOR_WORD;
        if(NO_CAPITALS_FLAG){
            LOOK_FOR_WORD = toLowerCase(LOOK_FOR_WORD); //set to lowercase since docs will be lowercase
        }
        std::println(std::cout, "Will scan for: {}", LOOK_FOR_WORD);
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
    for(size_t i = 2; i < tokens.size(); i++){
        flags.push_back(tokens.at(i));
    }
    justifyFlags(flags); //sets global values here
    file root(tokens.at(1)); //root fileName should be found here

    
    try{
       fileTreeStructure* structure = new fileTreeStructure(root);
       print::Debug("Object fileTreeStructure has been created");
        return structure;

    }catch(const std::exception& e){
        print::Error(e);
        print::Debug("Object fileTreeStructure failed to create.");
    }
    return nullptr;

}

