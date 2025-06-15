#pragma once

#include <string>
#include <iostream>

const bool ENABLE_IOS_HEURISTIC = true;
const bool ENABLE_PRUNING = true;

void logError(std::string msg) {
    std::cerr << "ERROR: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl;
}