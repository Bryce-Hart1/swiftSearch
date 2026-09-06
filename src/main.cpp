#include"parse.hpp" //check this cpp for all operatons done by the threads, and how they are assigned.
#include"tokenize.hpp" //check this cpp for how the tokenizer parses user input
#include"classes.hpp" //check this cpp for all class implementations 
#include"constants.hpp" //check this for print:: namespace, boolean flags and other global functions
#include"options.hpp" //new addition for the work in progress CLI rewrite

#include <optional>
#include <cstdlib>


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

    while(true){
        if(!std::getline(std::cin, arguments)){
            return {}; //EOF: let main handle it rather than looping forever
        }

        //'?' used to only be checked inside the retry branch, so it was ignored
        //unless the input was also too short to be a valid command
        if(!arguments.empty() && arguments.at(0) == '?'){
            print::Info();
            print::User("Enter a command:");
            continue;
        }

        if(goodArg(arguments)){
            return arguments;
        }

        print::User("Message passed in is not of valid length. Please enter cmd now or return ? for help.");
    }
}



class swiftSearch{
    swiftSearch(const swift_search::options& o){
        
    }

    public:
    int runSwiftSearchInCliMode(){

    }
    bool doOperation(const std::string_view& args){

    }




    private:
    swift_search::options _options;
    
};

int main(){
    std::string argumentArr = boot();
    Timer mainWatch;
    mainWatch.start(); //start timer for how long program takes

    //this used to be `*tokenize(...)`: a dereference of a raw pointer that could be
    //null, and a copy that leaked the new'd object behind it
    std::optional<fileTreeStructure> fileStruct = tokenize(argumentArr, std::ref(mainWatch));
    if(!fileStruct.has_value()){
        print::showDebug("Could not build a file list, exiting");
        return EXIT_FAILURE; //main used to return 0 on every path
    }

    assignOperation(operationTypeOfParse, fileStruct->createFileQueue()); //all work gets done here
    mainWatch.stop(); //stop and print
    print::User("Program has completed in " + mainWatch.readableTime());
    print::showDebug("Program successfully exited");
    return EXIT_SUCCESS;
}