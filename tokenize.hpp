#pragma once

#include <string> 
#include <mutex>
#include <filesystem>
#include <memory>
#include <vector>


std::string toLowerCase(std::string str);

void assignTokenOne(std::string tokenOne);

void justifyFlags(std::vector<std::string> flagsDetected);


void tokenMain(std::string input);


