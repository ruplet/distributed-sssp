#pragma once
#include <map>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>

#include "common.hpp"

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

    void init(int rank)
    {
        {
            if (!log_file.is_open())
            {
                log_file.open("debug_log_" + std::to_string(rank) + ".txt");
            }
        }
    }

    void force_log(const std::string &message)
    {
        if (log_file.is_open())
        {
            log_file << message << std::endl; // endl also flushes
        }
    }

    void log(const std::string &message)
    {
        if (ENABLE_LOGGING && log_file.is_open())
        {
            log_file << message << std::endl; // endl also flushes
        }
    }

    // A helper to log the state of buckets
    void log_buckets(const std::string &context, long long current_k, const std::map<long long, std::vector<int>> &buckets)
    {
        if (!ENABLE_LOGGING)
        {
            return;
        }
        std::stringstream ss;
        ss << "[" << context << "] k=" << current_k << " | Buckets: ";
        if (buckets.empty())
        {
            ss << "EMPTY";
        }
        else
        {
            for (const auto &pair : buckets)
            {
                ss << "[" << pair.first << ": " << pair.second.size() << "v] ";
            }
        }
        log(ss.str());
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