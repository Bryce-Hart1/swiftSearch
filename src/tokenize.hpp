#pragma once

#include "classes.hpp"
#include "parse.hpp"

#include <string> 
#include <vector>
#include <optional>


bool assignTokenOne(std::string tokenOne, Timer& watch);

void justifyFlags(std::vector<std::string> flagsDetected);

std::optional<fileTreeStructure> tokenize(const std::string& input, Timer& watch);