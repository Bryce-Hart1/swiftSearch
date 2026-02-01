#pragma once

#include "classes.hpp"
#include "parse.hpp"

#include <string> 
#include <vector>


std::string toLowerCase(std::string str);

bool assignTokenOne(std::string tokenOne);

void justifyFlags(std::vector<std::string> flagsDetected, Timer& watch);

fileTreeStructure* tokenize(std::string input, Timer& watch);