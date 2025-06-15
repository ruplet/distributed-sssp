#pragma once

#include <string>
#include <iostream>

// #define ENABLE_DELTA_STEPPING_OPTIMIZATIONS 1
const bool ENABLE_DELTA_STEPPING_OPTIMIZATIONS = true;

void logError(std::string msg) {
    std::cerr << "ERROR: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl;
}