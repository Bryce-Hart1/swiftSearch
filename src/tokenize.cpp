#include"tokenize.hpp"
#include"constants.hpp"

#include<filesystem>
#include<queue>
#include<optional>
#include<stdexcept>
#include<string_view>


//Will throw true if there is an error assigning token. If this occurs, program will exit from tokenize
bool assignTokenOne(std::string tokenOne, Timer &watch){
    static constexpr std::array<std::string_view, 7> Tokens = {"#info", "#sortedlist", "#-sortedlist", 
    "#listnumbers", "#getcharacters", "#getwords", "#findall"};

    tokenOne = toLowerCase(tokenOne);
    int foundAt = -1;

    for(size_t i = 0; i < Tokens.size(); i++){
        if(Tokens.at(i) == tokenOne){
            foundAt = static_cast<int>(i);
            break; //no point scanning the rest once it matches
        }
    }

    switch(foundAt){ //set operation type
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
        case 6: operationTypeOfParse = OP_TYPE::FIND_ALL;
        break;
        default : {
        operationTypeOfParse = OP_TYPE::UNSET;
        print::User("'" + tokenOne + "' is not a valid operation. Enter ? for the list.");
        return true;    }
        }

        if(operationTypeOfParse == OP_TYPE::FIND_ALL){ //Since operationType was just set, see if its findall. If yes, wait for input
        print::Debug("Operation type is: search");
        watch.stop();
        print::User("Please enter the word you are looking for:");
        std::getline(std::cin, LOOK_FOR_WORD);
        while(LOOK_FOR_WORD.empty()){ //an empty needle is undefined behaviour downstream
            print::User("The search term cannot be empty. Please enter a word:");
            if(!std::getline(std::cin, LOOK_FOR_WORD)){
                print::Error(std::runtime_error("No search term given"));
                return true;
            }
        }
        if(NO_CAPITALS_FLAG){
            LOOK_FOR_WORD = toLowerCase(LOOK_FOR_WORD); //set to lowercase since docs will be lowercase
        }
        print::User("Will look for -> " + LOOK_FOR_WORD + " <-");
        watch.start();
    }

    print::Debug("Token one value: " + std::to_string(foundAt));
    return false; 
}


/**
 * @brief sets flags based off of user input
 * compares for - + the command so -debug as a whole string 
 */
void justifyFlags(std::vector<std::string> flagsDetected){
    
    for(auto& flag : flagsDetected){
        for(char& chr : flag){
            chr = toLower(chr);
        }
    }
    print::Debug("justifyFlags() entered");
    std::array<std::string, 4> posFlag = {"-debug", "-caps", "-threadinfo", "-floatlist"}; 
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
 * @returns a filetree structure if valid, and std::nullopt if not
 */

std::optional<fileTreeStructure> tokenize(const std::string& input, Timer& watch){
    std::vector<std::string> tokens;


    //I want to set falgs first so we can get things like print::debug up ASAP
    std::stringstream stream(input);
    std::string temp;
    while(stream >> temp){
        tokens.push_back(temp); //pushes all tokens into a vector
    }

    if(tokens.size() < 2){ //assignTokenOne/at(1) below would throw
        print::User("A command needs an operation and a path, e.g. #findall ./docs");
        return std::nullopt;
    }

    //this used to be rebuilt from every token INSIDE the loop above: O(n^2), printed n times
    std::string message = "tokens pushed back: values are: ";
    for(const std::string& t : tokens){
        message += t + " |";
    }
    print::Debug(message);

    //sets some global booleans for tokens
    std::vector<std::string> flags;
    for(size_t i = 2; i < tokens.size(); i++){
        flags.push_back(tokens.at(i));
    }
    justifyFlags(flags); //sets global values in here

    if(assignTokenOne(tokens.at(0), std::ref(watch))){ //token one works here
        print::Debug("Error assigning token one");
        return std::nullopt; //let main decide the exit code instead of std::exit
    }

    file root(tokens.at(1)); //root fileName should be found here

    try{
        return fileTreeStructure(root); //by value: this used to leak a new'd pointer
    }catch(const std::exception& e){
        print::Error(e);
        print::Debug("Object fileTreeStructure failed to create.");
    }
    return std::nullopt;

}

