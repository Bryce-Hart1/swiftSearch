#include"parse.hpp" //check this cpp for all operatons done by the threads, and how they are assigned.
#include"tokenize.hpp" //check this cpp for how the tokenizer parses user input
#include"classes.hpp" //check this cpp for all class implementations 
#include"constants.hpp" //check this for print:: namespace, boolean flags and other global functions


//just checks length, not validity. If not valid, return after telling the user it is wrong
bool goodArg(const std::string &argumentArr){
    if(argumentArr.empty()){
        return false;
    }
    size_t wordCount = 0;
    bool inWord = false;
    
    for(char c : argumentArr){ //count words in argument
        if(!std::isspace(static_cast<unsigned char>(c))){
            if(!inWord){
                wordCount++;
                inWord = true;
            }
         }else{
            inWord = false;
        }
    }
    return wordCount >= 2; //return true if 2 or more words detected
}

//checks length/gets string from user. Enters a loop till user enters one of correct length
std::string boot(){
    std::string arguments;
    print::Logo();
    print::User("Enter a command:");
    std::getline(std::cin, arguments);
    
    while(!goodArg(arguments)){
        print::User("Message passed in is not of valid length. Please enter cmd now or return ? for help.");
        std::getline(std::cin, arguments);
        
        if(!arguments.empty() && arguments.at(0) == '?'){
            print::Info();
            print::User("Enter a command:");
            std::getline(std::cin, arguments);
        }
    }
    return arguments;
}

int main(){
    std::string argumentArr = boot();
    Timer mainWatch;
    mainWatch.start(); //start timer for how long program takes
    fileTreeStructure fileStruct = *tokenize(argumentArr, std::ref(mainWatch)); //all tokens get assigned here 
    assignOperation(operationTypeOfParse, fileStruct.createFileQueue()); //all work gets done here
    mainWatch.stop(); //stop and print
    print::User("Program has completed in " + mainWatch.readableTime());
    print::Debug("Program sucessfully exited");
}