#include <string>
#include <iostream>

void logError(std::string msg) {
    std::cerr << "ERROR: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl;
}