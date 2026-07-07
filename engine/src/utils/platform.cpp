#include "platform.h"

#include <cerrno>
#include <cstdio>

namespace ev2::platform {

struct timespec monotonic_clock_time() 
{
	struct timespec ts{};
	int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (rc < 0) {
		perror("clock_gettime");
	}
	return ts;
}

Result sleep_until(struct timespec *ts)
{
#ifdef _WIN32
    #error "ev2::platform::sleep_unit is not implemented on Windows"
#endif

#ifdef __APPLE__
	while (true) {
		struct timespec now = monotonic_clock_time();

		if (now.tv_sec > ts->tv_sec ||
			(now.tv_sec == ts->tv_sec && now.tv_nsec >= ts->tv_nsec)) {
			return SUCCESS;
		}

		struct timespec duration {
			.tv_sec = ts->tv_sec - now.tv_sec,
			.tv_nsec = ts->tv_nsec - now.tv_nsec
		};

		if (duration.tv_nsec < 0) {
			--duration.tv_sec;
			duration.tv_nsec += (long)1e9;
		}

		int rc = nanosleep(&duration, NULL); // Sleep thread
		if (rc == 0) {
			return SUCCESS;
		}

		if (rc != EINTR) {
            errno = rc;
			perror("nanosleep");
			return EUNKNOWN;
		}
    }
#endif

#ifdef __linux__
	while (true) {
		int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts, NULL);
		if (rc == 0) {
			return SUCCESS;
		}

		if (rc != EINTR) {
			errno = rc;
			perror("clock_nanosleep");
			return EUNKNOWN;
		}
	}
#endif
}

};
