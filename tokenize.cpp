#include"tokenize.h"
#include<mutex>
#include<filesystem>
#include<vector>
#include<iostream>

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





/**
 * @brief takes in first argument from command line
 * @example #sort, -sort, 
 */
int tokenOne(std::string token){
    if(token[0] != '#'){
        return -1; //nonvalid
    }
    token = toLowerCase(token);
    
    if(token == "#sortedlist"){
        return 1;
    }else if(token == "#-sortedlist"){
        return 2;
    }else if(token == "#listnumbers"){
        return 3;
    }else if(token == "#listwords"){
        return 4;
    }else if(token == "#findcharacterfrequency"){
        return 5;
    }else if(token == "#findwordfrequency"){
        return 6;
    }else{
        return -1;
    }
    
}


int tokenTwo(std::string token){
    if(!std::filesystem::exists(token)){
        std::cerr << "File path is not valid";
    }
    









}





























/**
 * @brief breaks down command line from main()
 * 
 * @details breaks down several parts:
 * 1st token: #Opertation tag followed by opertation
 * 2nd token: 
 * 
 * 
 * 
 * 
 * 
 */

void tokenMain(int count, char* argArr[]){
    std::vector<std::string> tokens;
    std::string temp;
    for(int i = 0; i < count; i++){
        temp = argArr[i];
        tokens.push_back(temp);
    }

    if(tokenOne(tokens[0]) == -1){
        std::cerr << "command invalid at: " << tokens[0] << "\n";
    }
    if(tokenTwo(tokens[1]) == -1){


    }
    

}




