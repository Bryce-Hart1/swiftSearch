#include"tokenize.hpp"
#include<mutex>
#include<filesystem>
#include<vector>
#include<iostream>
#include<sstream>
#include<algorithm> 
#include<functional>



//Node can function independant of fileTree, but we cannot start a new fileTree without a root
//I also need the tokenizer to catch the missing file name BEFORE we hit the stage where we set up a tree.

fileTreeNode::fileTreeNode(){

}

fileTreeNode::fileTreeNode(const std::string& name){

}


/**
 * @attention this member method returns a string of the path,
 * for example: swift/search/filename instead of object
 * 
 */
std::string fileTreeNode::getPath(){
    return this->path;
}




fileTree::fileTree(){ 
}

fileTree::fileTree(std::string root){

}



fileTree intializeTree(){
    

    
}


fileTreeNode fileTree::getDirectoryRoot(){
    return root;
}


bool fileTree::isChildNode(std::string path){
    if(!std::filesystem::is_directory(path)){
        return false;
    }
    return true;
}


std::queue<std::string> fileTree::queueToVisit(std::string root){
    std::queue<std::string> returnQueue;
    fileTreeNode current = getDirectoryRoot();

    if(isChildNode(current.getPath())){
        returnQueue.push(current.getPath());
        //go back up
    }else{
        
    }
}

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

    if(std::filesystem::is_directory(token)){ //first, check if its a single file we are scanning 
        
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
 * 
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




