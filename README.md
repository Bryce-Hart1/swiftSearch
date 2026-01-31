# swiftSearch
This is my multithreaded-focused project, which I will be using to show my knowledge of multithreading and OOP in C++ 
The main goal of this project is to show ADT design and multithreading to create runtime-effective searching. 

To start, you will enter a command. This command will include the directory you are working on and the operation
that you would like to perform on it. Here is an example of a valid command:

### ./swiftSearch #sortedList myFolder -threadtime -floatlist

This command will search myFolder for all numbers, including those with a decimal point.

## Formatting
The format will be as follows:
first a operation that begins with #. 
The list of operations can be found below.
Next, the name of the file or directory that you are searching inside of. This can be a single file, or multiple 
files.
Third, you will list a flag (if you want) Currently the only flag is -debug and this just lists the interworkings of the program as it runs.

The possible operations start with # when entering the command line, and they are as follows:

info - lists basic info of files such as if its a directory, name, and size.

sortedList - takes all numbers and sorts them into a printable file, smallest to largest.

-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed).

listNumbers - takes all numbers and sorts them into a file.

listWords - takes all the words and puts them in one file

getcharacter - finds the frequency of all values.

getcharacter - finds the frequency of all words in the list.

findOne/findAll finds either one or all references to the string provided by the user in the files.


# Usage




# Design 

## built classes
### charBucket
For this one, I knew there would be a fixed size of ascii values coming in from the user, so I made an array of every possible char and paired a simple atomic counter to it.
### atomicNode
This class has gone through many revisions thus far, and has been the most challenging of the classes. The current design uses nodes that each have mutex locks wrapped over them, to lock nodes when they are being modified over. 

### NumberList
For numberlist, I used async and std::futures to expect results from each thread, and adding them to one main vector when finished. From there, the user can sort or reverse the list
### File
File builds ontop of std::fileSystem, to keep track of information of individial files

### FileTreeStructure

## Printing
std::print was used to keep printing atomic, and was used in main to keep conformity throughout. I also wrapped many simular print functions in constants.hpp under the print namespace just to give a little clarity and less visual clutter.

## Thread Usage

