#pragma once

#include "classes.hpp"
#include "parse.hpp"

#include <string> 
#include <mutex>
#include <filesystem>
#include <memory>
#include <vector>


std::string toLowerCase(std::string str);

bool assignTokenOne(std::string tokenOne);


void justifyFlags(std::vector<std::string> flagsDetected);


void tokenMain(std::string input, file &root, fileTreeStructure &structure);


