#include "digital_output.h"
#include "time.h"

#include <lib/safe-c/safe_c.h>

#include <zephyr/kernel.h>
#include <zephyr/autoconf.h>

enum {
    DEFAULT_PULSE_PERIOD_MS = 500,
    DEFAULT_BLINK_ON_MS = DEFAULT_PULSE_PERIOD_MS / 2,
    THREAD_LOOP_SLEEP_MS = 1,
    THREAD_STACK_SIZE = 512,
    THREAD_PRIO = 3,
};

LOG_MODULE_REGISTER(ztl_digital_output);

static struct ZtlDigitalOutput* g_outputs[CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT] = {0};
K_MUTEX_DEFINE(g_outputs_mutex);

static void output_handler(void*, void*, void*);

K_THREAD_DEFINE(output_handler_tid, THREAD_STACK_SIZE,
                output_handler, NULL, NULL, NULL,
                THREAD_PRIO, 0, 0);

static inline int set_output(struct ZtlDigitalOutput* const output, bool const state) {
    if (state != output->hw_state) {
        output->hw_state = state;
        TRY(gpio_pin_set_dt(output->gpio, output->hw_state));
    }

    return 0;
}

static int handle_output(struct ZtlDigitalOutput* const self, int64_t const now) {
    uint16_t const period = self->pulse_state ? self->pulse_on_ms : self->pulse_period_ms - self->pulse_on_ms;

    if (0 != self->pulse_count && IS_TIME_EXPIRED_EX(self->tl_pulse_ms, period, now)) {
        self->tl_pulse_ms = now;
        if (!self->pulse_state) {
            // Full cycle has been completed
            if (self->pulse_count > 0) {
                self->pulse_count--;
                if (0 == self->pulse_count) {
                    TRY(set_output(self, self->state));
                    return 0;
                }
            }
        }
        self->pulse_state = !self->pulse_state;
        TRY(set_output(self, self->pulse_state));
    }

    return 0;
}

static void output_handler(void* arg1, void* arg2, void* arg3) {
    while (true) {
        k_mutex_lock(&g_outputs_mutex, K_FOREVER);
        int64_t const now = k_uptime_get();
        for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
            if (g_outputs[i]) {
                TRY_PASS(handle_output(g_outputs[i], now));
            }
        }
        k_mutex_unlock(&g_outputs_mutex);
        k_msleep(THREAD_LOOP_SLEEP_MS);
    }
}


int ztl_digital_output__init(struct ZtlDigitalOutput* const self, struct gpio_dt_spec const* const gpio) {
    int rc = ER_NO_MEM;

    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != gpio, ER_INVAL);

    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
        if (g_outputs[i]) {
            ASSERT_EX(!(g_outputs[i]->gpio->port == gpio->port && g_outputs[i]->gpio->pin == gpio->pin), ER_ALREADY);
        }
    }

    for (int i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
        if (NULL == g_outputs[i]) {
            g_outputs[i] = self;
            memset(self, 0, sizeof(*self));
            self->gpio = gpio;
            self->pulse_period_ms = DEFAULT_PULSE_PERIOD_MS;
            self->pulse_on_ms = DEFAULT_BLINK_ON_MS;
            TRY_EX(gpio_pin_configure_dt(self->gpio, GPIO_OUTPUT));
            TRY_EX(gpio_pin_set_dt(gpio, self->state));
            rc = 0;
            break;
        }
    }

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__set(struct ZtlDigitalOutput* const self, bool const state) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT_EX(NULL != self, ER_INVAL);

    if (0 == self->pulse_count) {
        TRY(set_output(self, state));
    }

    self->state = state;

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__start_blink(struct ZtlDigitalOutput* const self, int const pulse_count) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT_EX(NULL != self, ER_INVAL);

    self->pulse_count = pulse_count;
    self->pulse_state = true;
    self->tl_pulse_ms = k_uptime_get();
    TRY_EX(set_output(self, self->pulse_state));

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__config_blink(struct ZtlDigitalOutput* const self, uint16_t const pulse_period_ms, uint16_t const pulse_on_ms) {
    int rc = 0;

    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT_EX(NULL != self, ER_INVAL);
    ASSERT_EX(pulse_on_ms > 0, ER_INVAL);
    ASSERT_EX(pulse_on_ms < pulse_period_ms, ER_INVAL);

    self->pulse_period_ms = pulse_period_ms;
    self->pulse_on_ms = pulse_on_ms;

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__stop_blink(struct ZtlDigitalOutput* const self) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT_EX(NULL != self, ER_INVAL);

    self->pulse_count = 0;
    TRY_EX(set_output(self, self->state));

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}
