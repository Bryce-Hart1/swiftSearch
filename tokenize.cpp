#include"tokenize.hpp"
#include"constants.hpp"

#include<mutex>
#include<filesystem>
#include<vector>
#include<sstream>
#include<algorithm> 
#include<functional>
#include<queue>



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



std::string makeSpellCheckOnFirstToken(std::string token, std::array<std::string, 7> expectedFirst){ 
    const int exceptedMisses = 2; //change amount of character misses here
    for(std::string expectedVal : expectedFirst){
        int miss = 0;
        int ind = 0;
        expectedVal = toLowerCase(expectedVal);
        while(ind < expectedVal.size() && ind < token.size() && miss <= exceptedMisses){
            if(expectedVal[ind] != token[ind]){
                miss++;
            }
            ind++;
        }
        if(miss >= exceptedMisses){
            return expectedVal;
        }
        
    }
    if(DEBUG_ACTIVE){
        std::cout << "DEBUG ~~ no match was found, and the token was: " << token << std::endl;
    }
    return token; //no match found
}

/**
 * @brief this is an important function, it will set opertation type enum, start making fileTree, and set debug mode
 */
int checkTokenValid(int place, std::string tokenExtracted){
    const int operationType = 0;
    const int fileDirectory = 1;
    const int debugToken = 2;
    std::array<std::string, 7> firstToken = {};
    
    
    switch (place){
        case 0://case first token

        

            break;
        case 1://case second token



        break;



        case debugToken:

            if(tokenExtracted == "debug"){
                DEBUG_ACTIVE = true;
                std::cout << "DEBUG ~~ debug has been flipped true" << std::endl;
            }
            break;
    
        default:
        
            std::cout << "DEBUG ~~ checkTokenValid is out of bounds. Values are " << place << " " << tokenExtracted << std::endl;
            break;
    }
    return 1;
}





/**
 * @brief breaks down command line from main()
 * 
 * @details breaks down several parts:
 * 1st token: #Opertation tag followed by opertation
 * 2nd token: filepath 
 * 3rd token: debug mode
 */


void tokenMain(std::string input){
    std::vector<std::string> tokens;
    const int maxTokenLimit = 10;
    std::array<int, maxTokenLimit> tokensToInt;
    std::stringstream stream(input);
    std::string temp;
    while(stream >> temp){
        tokens.push_back(temp);
    }
    int currentTokenOn = 0;
    for(std::string token : tokens){
        checkTokenValid(currentTokenOn, token);
        currentTokenOn++;
    }
    
    

}

