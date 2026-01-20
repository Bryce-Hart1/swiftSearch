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


void assignTokenOne(std::string tokenOne){
    std::array<std::string, 10> Tokens = {"#info"};
    tokenOne = toLowerCase(tokenOne);
    int goThoughTokens = 0;
    for(std::string token : Tokens){
        for(int i = 0; i < token.length(); i++){
            if(goThoughTokens == 10){
                std::cout << "first Token is not valid" << std::endl;
            }
            if(token[i] != Tokens[goThoughTokens][i]){ //if it is valid, contine.
                
            }
        }
    }    
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
    std::string fileToLook;

    std::stringstream stream(input);
    std::string temp;
    while(stream >> temp){
        tokens.push_back(temp); //pushes all tokens into a vector
    }




    
    

}

