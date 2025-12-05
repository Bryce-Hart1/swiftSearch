#include<classes.cpp>
#include<mutex>



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
    
    if(token == "#sortedList"){

    }else if(token == "#-sortedList"){

    }else if(token == "#listNumbers"){
        

    }else{
        return -1;
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

void tokenMain(int count, char* vector[]){

}




