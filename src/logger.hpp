#pragma once
#include <map>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>

#include "common.hpp"

#define PROGRESS(...)                                                                        \
    do                                                                                       \
    {                                                                                        \
        if (logging_level == LoggingLevel::Progress || logging_level == LoggingLevel::Debug) \
        {                                                                                    \
            DebugLogger::getInstance().log(__VA_ARGS__);                                     \
        }                                                                                    \
    } while (0)

#define PROGRESSN(...)                                                                       \
    do                                                                                       \
    {                                                                                        \
        if (logging_level == LoggingLevel::Progress || logging_level == LoggingLevel::Debug) \
        {                                                                                    \
            DebugLogger::getInstance().logn(__VA_ARGS__);                                    \
        }                                                                                    \
    } while (0)

#define DEBUG(...)                                       \
    do                                                   \
    {                                                    \
        if (logging_level == LoggingLevel::Debug)        \
        {                                                \
            DebugLogger::getInstance().log(__VA_ARGS__); \
        }                                                \
    } while (0)

#define DEBUGN(...)                                       \
    do                                                    \
    {                                                     \
        if (logging_level == LoggingLevel::Debug)         \
        {                                                 \
            DebugLogger::getInstance().logn(__VA_ARGS__); \
        }                                                 \
    } while (0)

#define ERROR(...)                                                                     \
    do                                                                                 \
    {                                                                                  \
        if (true)                                                                      \
        {                                                                              \
            DebugLogger::getInstance().logn("ERROR", __FILE__, __LINE__, __VA_ARGS__); \
        }                                                                              \
    } while (0)

#define MPI_CALL(call)                                      \
    do                                                      \
    {                                                       \
        int err = (call);                                   \
        if (err != MPI_SUCCESS)                             \
        {                                                   \
            char err_string[MPI_MAX_ERROR_STRING];          \
            int resultlen;                                  \
            MPI_Error_string(err, err_string, &resultlen);  \
            ERROR("MPI ERROR in ", #call, ":", err_string); \
            MPI_Abort(MPI_COMM_WORLD, err);                 \
        }                                                   \
    } while (0)

template <typename T>
void append_to_stream(std::ostream &oss, const T &arg)
{
    oss << arg;
}

template <typename T, typename... Args>
void append_to_stream(std::ostream &oss, const T &first, const Args &...rest)
{
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

    void init(const std::string &filename)
    {
        {
            if (!log_file.is_open())
            {
                log_file.open(filename);
                log_file << std::unitbuf;
            }
        }
    }

    template <typename... Args>
    void log(const Args &...args)
    {
        if (log_file.is_open())
        {
            std::ostringstream oss;
            append_to_stream(oss, args...);
            log_file << oss.str();
        }
    }

    template <typename... Args>
    void logn(const Args &...args)
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