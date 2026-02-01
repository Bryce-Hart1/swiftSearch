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
Next, the name of the file or directory that you are searching inside of. This can be a single file or multiple files. 
files.
Third, you will list a flag (if you want)

```The possible operations (start with #) are as follows:

info - lists basic info of files, such as if its a directory, name, and size.

sortedList - takes all numbers and sorts them into a printable file, smallest to largest.

-sortedList - takes all numbers and sorts them into a printable file, largest to smallest (reversed).

listNumbers - takes all numbers and sorts them into a file.

listWords - takes all the words and puts them in one file

getcharacter - finds the frequency of all values.

getcharacter - finds the frequency of all words in the list.

findAll finds either one or all references to the string provided by the user in the files.
```
```The possible flags that can be set are as follows:
-debug - just general debug information relating to functions and methods inside the program.
-threadinfo - will give you information on when threads enter/exit their planned scope
-caps - words on any word-related commands, and will ignore capital letters for storing and displaying.
-float list - works for any number operations, and will keep the precision of decimals up to a certain point
```

# Usage
## System Requirements
- **Operating System**: UNIX-based systems (Linux, macOS, BSD)
- **Compiler**: C++20 compatible compiler (GCC 10+, Clang 10+)
- **Standard Library**: Full C++20 standard library support

## Start guide
Please refer to the other markdown file in this repo titled: `STARTGUIDE.md` for further explanantion beyond what I go over here.
### Compatibility Note
This program is designed for UNIX-based systems and has not been tested on Windows. While most C++ standard library features are cross-platform, the build system and file path handling are UNIX-specific.
# Design Choices

## built classes
In this project, I wanted to try OOP in a language that I hadn't yet used, what being c++. For competetive programing, I had used structs, but that is not what I wanted to try here. I wanted everything to act as its own, standalone unit that I could call on later outside of this project if I wanted to. To do this, I created classes for everything that I thought could be. 
### charBucket
For this one, I knew there would be a fixed size of ascii values coming in from the user, so I made an array of every possible char and paired a simple atomic counter to it.
This would be a part of the charbucket structure:
```
[g][h][i][k]...
[1][3][7][9]...
```
The goal was simple - create a fast lookup for any char value, and update its counter atomically. This was pretty easily done, with me wrapping over a single bucket, looking like this:
```
[g]
[1]
```
This way I could 0 out these counts and print the ones that were found more than 0 times, in a way that is already allocated and cheap on time and memory.
### atomicNode
This class has gone through many revisions thus far, and has been the most challenging of the classes. The current design uses nodes that each have mutex locks wrapped over them, to lock nodes when they are being modified over. My main goal was to use a trie, which I hadnt really used in class and only had one run in with at ICPC. Each node has a few properties - a mutex lock, a char value, and a count. 
### NumberList
For numberlist, I used async and std::futures to expect results from each thread, and add them to one main vector when finished. From there, the user can sort or reverse the list, depending on what command they enter. This class also went through many revisions, and most of the ideas got scrapped. For example, first, I was going to extend atomicNode to keep numbers - but I decided to scrap this as the idea was kind of redundant to what I had already made, and while sure it would be fast, I didnt think it would be as fast as std::vector. So, with this in mind, I did research on how to add vectors together and settled on using std::async and std::futures to return vectors to one big vector and print.
### File
File builds ontop of std::fileSystem, to keep track of information of individial files. It keeps stats like size, name, and path, which are mostly wrappers over preexisting methods. 
### FileTreeStructure
FileTreeStructure is pretty interesting. Its not even a tree, actually. FileTreeStructure takes all files that would be scanned and puts them into a vector. From there, a method sorts them into a queue, and from there is assigned to individual threads
## Printing
std::print was used to keep printing atomic, and was used in main to keep conformity throughout, and also gave me a chance to try out more modern c++ features. I also wrapped many simular print functions in constants.hpp under the print namespace just to give a little clarity and less visual clutter.
## Thread Usage
Coming into this project, I was going to assign multiple threads to an individual file, but not only would that complicate the work, but it wasn't really needed.
For every file class, only one thread is assigned to each. 
