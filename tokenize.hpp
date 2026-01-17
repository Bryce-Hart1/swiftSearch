#pragma once

#include <string> 
#include <mutex>
#include <filesystem>
#include <memory>
#include <vector>


std::string toLowerCase(std::string str);

std::string makeSpellCheckOnFirstToken(std::string input, std::array<std::string, 7> expected);

int checkTokenValid(int place, std::string tokenExtracted);


void tokenMain(std::string input);


