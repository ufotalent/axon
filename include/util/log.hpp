#pragma once
#include <string>
#include "util/noncopyable.hpp"


#define APPEND_LOG(level, msg, args...)\
    axon::util::Log::get_instance().log(__FILE__, __LINE__, level, msg, ## args)

#define LOG_FATAL(msg, args...)\
    APPEND_LOG(axon::util::Log::FATAL, msg, ## args)
#define LOG_ERROR(msg, args...)\
    APPEND_LOG(axon::util::Log::ERROR, msg, ## args)
#define LOG_WARN(msg, args...)\
    APPEND_LOG(axon::util::Log::WARN, msg, ## args)
#define LOG_INFO(msg, args...)\
    APPEND_LOG(axon::util::Log::INFO, msg, ## args)
#define LOG_DEBUG(msg, args...)\
    APPEND_LOG(axon::util::Log::DEBUG, msg, ## args)
namespace axon {
namespace util {
class Log {
public:
    enum LogLevel {
        FATAL = 0,
        ERROR = 1,
        WARN  = 2,
        INFO  = 3,
        DEBUG = 4
    };
    static Log& get_instance();
    void log(const char* fn, int ln, LogLevel level, const char* msg, ...);
    std::string make_string(const char* fn, int ln, LogLevel level, const char* msg, va_list args);
private:
    Log() {};

};
}
}
