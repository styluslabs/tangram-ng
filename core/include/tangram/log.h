#pragma once

#include "platform.h"

#include <atomic>
#include <cstring>

/*
 * Log utilities:
 * LOGD: Debug log, LOG_LEVEL >= 3
 * LOGW: Warning log, LOG_LEVEL >= 2
 * LOGE: Error log, LOG_LEVEL >= 1
 * LOGN: Notification log (displayed once), LOG_LEVEL >= 0
 * LOG: Default log, LOG_LEVEL >= 0
 * LOGS: Screen log, no LOG_LEVEL
 */

namespace Tangram {
    std::string fstring(const char* fmt, ...);

    template <typename... Params>
    void logMsg(Params&&... params) { logStr(fstring(std::forward<Params>(params)...)); }
}

#ifdef __clang__
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

//#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
// From: https://blog.galowicz.de/2016/02/20/short_file_macro/
static constexpr const char * past_last_slash(const char * const str, const char * const last_slash) {
    return *str == '\0' ? last_slash :
        *str == '/'  ? past_last_slash(str + 1, str + 1) :
        past_last_slash(str + 1, last_slash);
}

static constexpr const char * past_last_slash(const char * const str) {
    return past_last_slash(str, str);
}

#ifdef TANGRAM_WINDOWS
#define __FILENAME__ __FILE__
#else
#define __FILENAME__ ({constexpr const char * const sf__ {past_last_slash(__FILE__)}; sf__;})
#endif

#define TANGRAM_MAX_BUFFER_LOG_SIZE 99999

#define LOG_LINE(level, fmt, ...) \
do { Tangram::logAll(Tangram::fstring(level " %s:%d: " fmt "\n", __FILENAME__, __LINE__, ## __VA_ARGS__)); } while(0)

#if LOG_LEVEL >= 4
#define LOGV(fmt, ...) LOG_LINE("\x1b[34m" "VERBOSE", fmt "\x1b[0m", ## __VA_ARGS__)
#else
#define LOGV(fmt, ...)
#endif

#if LOG_LEVEL >= 3
#define LOGD(fmt, ...) LOG_LINE("\x1b[32m" "DEBUG", fmt "\x1b[0m", ## __VA_ARGS__)
#else
#define LOGD(fmt, ...)
#endif

#if LOG_LEVEL >= 2
#define LOGW(fmt, ...) LOG_LINE("WARNING", fmt, ## __VA_ARGS__)
#else
#define LOGW(fmt, ...)
#endif

#if LOG_LEVEL >= 1
#define LOGE(fmt, ...) LOG_LINE("ERROR", fmt, ## __VA_ARGS__)
#else
#define LOGE(fmt, ...)
#endif

#if LOG_LEVEL >= 0
// The 'please notice but don't be too annoying' logger:
#define LOGN(fmt, ...) \
    do { \
        static std::atomic<size_t> _lock(0); \
        if (_lock++ < 42) { LOG_LINE("NOTIFY", fmt, ## __VA_ARGS__); } \
    } while (0)

#define LOG(fmt, ...) LOG_LINE("TANGRAM", fmt, ## __VA_ARGS__)
#else
#define LOG(fmt, ...)
#define LOGN(fmt, ...)
#endif

#ifdef TANGRAM_TRACING
#include <mutex>
#include <chrono>

extern std::chrono::time_point<std::chrono::system_clock> tangram_log_time_start, tangram_log_time_last;
extern std::mutex tangram_log_time_mutex;

#define LOGTIME(fmt, ...) do { \
    int l = strlen( __FILENAME__);  \
    Tangram::logMsg("TIME %-18.*s " fmt "\n",                            \
                    l > 4 ? l-4 : l, __FILENAME__, ##__VA_ARGS__); } while (0)

// Overall timing init/reset
#define LOGTOInit() do {                                                \
        std::lock_guard<std::mutex> lock(tangram_log_time_mutex);       \
        tangram_log_time_last = tangram_log_time_start = std::chrono::system_clock::now(); } while(0)

// Overall timing
#define LOGTO(fmt, ...) do {                                            \
        std::lock_guard<std::mutex> lock(tangram_log_time_mutex);       \
        std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now(); \
        std::chrono::duration<double> t1 = now - tangram_log_time_start; \
        std::chrono::duration<double> t2 = now - tangram_log_time_last; \
        tangram_log_time_last = now;                                    \
        LOGTIME("%7.2f %7.2f " fmt, t1.count()*1000.f, t2.count()*1000.f, ## __VA_ARGS__); } while(0)

// Local timing init
#define LOGTInit(fmt, ...)                                              \
    std::chrono::time_point<std::chrono::system_clock> _time_last, _time_start; \
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now(); \
    std::chrono::duration<double> t0 = now - tangram_log_time_start;    \
    _time_start = _time_last = now;                                     \
    LOGTIME("%7.2f                 " fmt, t0.count()*1000.f, ## __VA_ARGS__)

// Local timing
#define LOGT(fmt, ...) do {                                               \
        std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now(); \
        std::chrono::duration<double> t0 = now - tangram_log_time_start; \
        std::chrono::duration<double> t1 = now - _time_start;           \
        std::chrono::duration<double> t2 = now - _time_last;            \
        _time_last = now;                                               \
        LOGTIME("%7.2f %7.2f %7.2f " fmt, t0.count()*1000.f, t1.count()*1000.f, t2.count()*1000.f, ## __VA_ARGS__); } while(0)
#else
#define LOGT(...)
#define LOGTInit(...)
#define LOGTOInit()
#define LOGTO(...)
#endif
