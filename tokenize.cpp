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



std::string makeSpellCheck(std::string token, std::vector<std::string> &expected){ // change param to a array soon
    const int exceptedMisses = 2; //change amount of character misses here
    for(std::string expectedVal : expected){
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
    return token; //no match found
}


int checkTokenValid(int place, std::string tokenExtracted){
    const int debugToken = 4;
    std::array<std::string, 7> firstToken;
    std::array<std::string, 4> secondTokens;
    std::array<std::string, 5> optionalThird;
    
    switch (place){
        case 0://case first token

        

            break;
        case 1://case second token



        break;



        case 2:




        case debugToken:

            if(tokenExtracted == "debug"){

            }


            break;
    
        default:
        
            std::cout << "checkTokenValid is out of bounds. Values are " << place << " " << tokenExtracted << std::endl;
            break;
    }

}





/**
 * @brief breaks down command line from main()
 * 
 * @details breaks down several parts:
 * 1st token: #Opertation tag followed by opertation
 * 2nd token: filepath 
 * 
 * 
 * 
 * 
 * to be refactored
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
    
    

}

