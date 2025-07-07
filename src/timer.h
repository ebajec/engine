#ifndef TIMER_H
#define TIMER_H

#include <utils/log.h>

#include <chrono>
#include <unordered_map>
#include <string>

static std::unordered_map<std::string,uint64_t> g_timer_values;

#define TIMER_START(msg,...) \
g_timer_values[msg] = std::chrono::high_resolution_clock::now().time_since_epoch()/std::chrono::nanoseconds(1);
#define TIMER_END(msg,...) {\
    uint64_t g_timer_end = std::chrono::high_resolution_clock::now().time_since_epoch()/std::chrono::nanoseconds(1); \
    log_info(msg,##__VA_ARGS__);\
    log_info(" : %.4f ms",(float)(g_timer_end - g_timer_values[msg])/1e6);\
} 
#endif // TIMER_H
