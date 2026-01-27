#ifndef ZTL_TIME_H_
#define ZTL_TIME_H_

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/timeutil.h>

typedef int64_t millis_t;
typedef int32_t interval_ms_t;

static inline bool is_period_expired(millis_t tle, interval_ms_t period) {
    millis_t const now = k_uptime_get();
    return now < tle || now - tle >= period;
}

static inline bool is_period_expired_ex(millis_t tle, interval_ms_t period, millis_t now) {
    return now < tle || now - tle >= period;
}

static inline void sleep_in_cycle(millis_t time_start, interval_ms_t step_sleep, int32_t cycle_i) {
    millis_t const now = k_uptime_get();
    millis_t const estimated_now = time_start + cycle_i * step_sleep;
    interval_ms_t const sleep_ms = now > estimated_now ? step_sleep - 1 : step_sleep;
    k_msleep(sleep_ms);
}

#endif // ZTL_ZTIME_H_
