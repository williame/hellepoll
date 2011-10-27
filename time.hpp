/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef TIME_HPP
#define TIME_HPP

#include <stdint.h>

typedef int64_t time64_t;

int time64_to_millisecs(const time64_t& time);

uint64_t time64_to_millisecs64(const time64_t& time);

time64_t millisecs_to_time64(int millisecs);

time64_t time64_now();

#endif //TIME_HPP

