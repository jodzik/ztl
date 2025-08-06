#ifndef ZTL_TIME_H_
#define ZTL_TIME_H_

#include <zephyr/types.h>

typedef uint64_t millis_t;

#define IS_TIME_EXPIRED_EX(tle, period, now) (now - tle >= period || now < tle)

#endif // ZTL_ZTIME_H_