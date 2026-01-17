#pragma once

#include <string> 
#include <mutex>
#include <filesystem>
#include <memory>
#include <vector>


std::string toLowerCase(std::string str);

std::string makeSpellCheck(std::string input, std::vector<std::string> &expected);

int checkTokenValid(int place, std::string tokenExtracted);


void tokenMain(std::string input);


