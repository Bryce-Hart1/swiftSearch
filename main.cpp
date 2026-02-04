#include"parse.hpp"
#include"tokenize.hpp"
#include"classes.hpp"
#include"constants.hpp"


//just checks length, not validity. If not valid, return after telling the user it is wrong
bool goodArg(const std::string &argumentArr){
    if(argumentArr.empty()){
        return false;
    }
    
    size_t wordCount = 0;
    bool inWord = false;
    
    for(char c : argumentArr){
        if(!std::isspace(static_cast<unsigned char>(c))){
            if(!inWord){
                wordCount++;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
    
    return wordCount >= 2;
}

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
    fileTreeStructure fileStruct = *tokenize(argumentArr, std::ref(mainWatch));
    print::Debug("Debug mode is on");
    assignOperation(operationTypeOfParse, fileStruct.createFileQueue());

    mainWatch.stop();
    std::println(std::cout, "Opertation has finished in {}", mainWatch.readableTime());
    print::Debug("Program sucessfully exited");
}