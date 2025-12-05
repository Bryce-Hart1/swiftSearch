#include<parse.cpp>


/*
Possible operations:
#d collect instances of term
#df print every instance of term
#createList builds a file of all terms found in the files
three options 
default sort (no token passed first of trie)
> greatest amount to least amount 
< least amount to greatest amount
#createChar builds a file that shows amount of characters found in the files 




format of command -
#operation fileToparse



*/



/**
 * @details added this to give users a way to get used to the program and learn commands
 * 
 */
void printInfo(){
    using namespace std;



    cout << "Please input a file to parse in the following format" << endl;
    cout << "#sortedList @fileName" << endl;
    sleep(1);
    cout << "possible # operations: " << endl;
    cout << " #sortedList - takes all numbers and sorts them into a printable file, smallest to largest" << endl;
    cout << " #-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)" << endl;
    cout << "#listNumbers - takes all numbers and sorts them into a file in the order they appear" << endl;
    cout << "#listWords - takes all the words in the file and displays them in the order they appear." << endl;
    cout << "#findFreq - finds the frequency of all values" << endl;
    cout << "findWordFreq - finds the frequency of all words in the list" << endl;

}


int main(int argc, char *argv[]){
    using namespace std;
    char user; 
    const char userEntersCommand = 'c';
    const char userRequestsInfo = 'i';
    tokenMain(argc, argv);    


    //if user does not understand to enter on commandLine, we need then to have a valid command
        cout << "Enter command: " << userEntersCommand << " View Formatting " << userRequestsInfo;



}