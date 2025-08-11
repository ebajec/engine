#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <functional>

extern void g_schedule_task(std::function<void(void)> &&task);
extern void g_schedule_blocking(std::function<void(void)> &&task);

#endif // THREAD_POOL_H
