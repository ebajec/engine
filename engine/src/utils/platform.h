#ifndef EV2_PLATFORM_H
#define EV2_PLATFORM_H

#include <ev2/context.h>
#include <time.h>

namespace ev2::platform {

struct timespec monotonic_clock_time(); 
Result sleep_until(struct timespec *ts);

};

#endif
