# swiftSearch
This is my multithreaded-focused project, which I will be using to show my knowledge of multithreading and OOP in C++ 
The main goal of this project is to show ADT design and multithreading to create runtime-effective searching. 

To start, you will enter a command. This command will include the directory you are working on and the operation
that you would like to perform on it.


The format will be as follows:
first a operation that begins with #. 
The list of operations can be found below.
Next, the name of the file or directory that you are searching inside of. This can be a single file, or multiple 
files.
Third, you will list a flag (if you want) Currently the only flag is -debug and this just lists the interworkings of the program as it runs.

The possible operations start with # when entering the command line, and they are as follows:

info - lists basic info of files such as if its a directory, name, and size
sortedList - takes all numbers and sorts them into a printable file, smallest to largest
-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed)
listNumbers - takes all numbers and sorts them into a file in the order they appear
listWords - takes all the words in the file and displays them in the order they appear.
findCharacterFrequency - finds the frequency of all values
findWordFreq - finds the frequency of all words in the list


# Usage
for clang: compile with: 
clang++ -std=c++26 main.cpp tokenize.cpp parse.cpp classes.cpp -o main 




# Design 

std::print was used to keep printing atomic, and was used in main to keep conformity throughout.
