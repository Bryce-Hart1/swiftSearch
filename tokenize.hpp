#pragma once

#include "classes.hpp"
#include "parse.hpp"

#include <string> 
#include <vector>


std::string toLowerCase(std::string str);

bool assignTokenOne(std::string tokenOne, Timer watch);

void justifyFlags(std::vector<std::string> flagsDetected);

fileTreeStructure* tokenize(std::string input, Timer& watch);