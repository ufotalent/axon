#include "util/log.hpp"
#include <unistd.h>
#include <cstring>
#include <cstdarg>
using namespace axon::util;

Log& Log::get_instance() {
    static Log instance;
    return instance;
}

void Log::log(const char* fn, int ln, LogLevel level, const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    std::string log = make_string(fn, ln, level, msg, args);
    va_end(args);

    fprintf(stdout, "%s", log.c_str());
    fflush(stdout);
}

std::string Log::make_string(const char* fn, int ln, LogLevel level, const char* msg, va_list args) {
    const size_t MAX_BUFFER_SIZE = 10240;
    const char* levelText[] = { "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "UNKNOWN" };

    time_t t = time(NULL);
    struct tm local_tm;
    localtime_r(&t, &local_tm);
    char timestr[30];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &local_tm);

    char buffer[MAX_BUFFER_SIZE];
    size_t len = 0;
    int sz = 0;

    sz = snprintf(buffer, MAX_BUFFER_SIZE - len, "[%s %s] ", timestr, levelText[level]);
    if (sz >= 0) len += sz;

    sz = vsnprintf(buffer + len, MAX_BUFFER_SIZE - len, msg, args);
    if (sz >= 0) len += sz;

    const char* fnbase= strrchr(fn, '/');
    if (fnbase != NULL)
        fn = fnbase + 1;
    sz = snprintf(buffer + len, MAX_BUFFER_SIZE - len, " [%s:%d @%d]\n", fn, ln, getpid());
    if (sz >= 0) len += sz;

    return std::string(buffer);
}
