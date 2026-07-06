#include "platform.h"
#include <cstdio>

namespace ev2::platform {

struct timespec monotonic_clock_time() 
{
	struct timespec ts;
	int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (rc < 0) {
		perror("clock_gettime");
	}
	return ts;
}
Result sleep_until(struct timespec *ts)
{
	int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts, NULL); 
	if (rc != 0) {
		perror("clock_nanosleep");
		return EUNKNOWN;
	}
	return SUCCESS;
}

};

