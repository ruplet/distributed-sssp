#pragma once
#include <map>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>

#include "common.hpp"

template<typename T>
void append_to_stream(std::ostringstream& oss, const T& arg) {
    oss << arg;
}

template<typename T, typename... Args>
void append_to_stream(std::ostringstream& oss, const T& first, const Args&... rest) {
    oss << first << ' ';
    append_to_stream(oss, rest...);
}

class DebugLogger
{
private:
    std::ofstream log_file;

public:
    // This is a singleton pattern to ensure one logger per process
    static DebugLogger &getInstance()
    {
        static DebugLogger instance;
        return instance;
    }

    void init(const std::string& filename)
    {
        {
            if (!log_file.is_open())
            {
                log_file.open(filename);
            }
        }
    }

    template<typename... Args>
    void log(const Args&... args)
    {
        if (log_file.is_open())
        {
            std::ostringstream oss;
            append_to_stream(oss, args...);
            log_file << oss.str();
        }
    }

    template<typename... Args>
    void logn(const Args&... args)
    {
        if (log_file.is_open())
        {
            std::ostringstream oss;
            append_to_stream(oss, args...);
            log_file << oss.str() << std::endl;
        }
    }

private:
    // Private constructor/destructor for singleton
    DebugLogger() {}
    ~DebugLogger()
    {
        if (log_file.is_open())
        {
            log_file.close();
        }
    }
    DebugLogger(const DebugLogger &) = delete;
    void operator=(const DebugLogger &) = delete;
};