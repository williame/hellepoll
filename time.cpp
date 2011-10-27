/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "time.hpp"
#include "error.hpp"

#ifdef TIME_NANO
	#include <time.h>
#else
	#include <sys/time.h>
#endif

int time64_to_millisecs(const time64_t& time) {
	return time64_to_millisecs64(time);
}

uint64_t time64_to_millisecs64(const time64_t& time) {
#ifdef TIME_NANO
	return (time/1000000LL);
#else
	return (time/1000LL);
#endif
}

time64_t millisecs_to_time64(int millisecs) {
#ifdef TIME_NANO
	return (millisecs*1000000LL);
#else
	return (millisecs*1000LL);
#endif
}

time64_t time64_now() {
	time64_t ret;
#ifdef TIME_NANO
	timespec ts;
	check(clock_gettime(CLOCK_MONOTONIC,&ts));
	ret = (ts.tv_sec * 1000000000LL) + ts.tv_nsec;
#else
	timeval tv;
	check(gettimeofday(&tv,NULL));
	ret = (tv.tv_sec * 1000000LL) + tv.tv_usec;
#endif
	return ret;
}


