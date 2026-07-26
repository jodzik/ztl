#include "digital_output.h"
#include "ztl_time.h"

#include <lib/safe-c/safe_c.h>

#include <zephyr/kernel.h>
#include <zephyr/autoconf.h>

enum {
    DEFAULT_PULSE_PERIOD_MS = 500,
    DEFAULT_BLINK_ON_MS = DEFAULT_PULSE_PERIOD_MS / 2,
};

LOG_MODULE_REGISTER(ztl_digital_output);

static struct ZtlDigitalOutput* g_outputs[CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT] = {0};
K_MUTEX_DEFINE(g_outputs_mutex);

static struct k_work_delayable g_output_work;
static void output_work_handler(struct k_work* work);

static inline int set_output(struct ZtlDigitalOutput* const output, bool const state) {
    int rc = 0;
    if (state != output->hw_state) {
        output->hw_state = state;
        TRY(gpio_pin_set_dt(output->gpio, output->hw_state));
    }

 finally:
    return rc;
}

static int handle_output(struct ZtlDigitalOutput* const self, int64_t const now) {
    int rc = 0;
    uint16_t const period = self->pulse_state ? self->pulse_on_ms : self->pulse_period_ms - self->pulse_on_ms;

    if (0 != self->pulse_count && is_period_expired_ex(self->tl_pulse_ms, period, now)) {
        self->tl_pulse_ms = now;
        if (!self->pulse_state) {
            // Full cycle has been completed
            if (self->pulse_count > 0) {
                self->pulse_count--;
                if (0 == self->pulse_count) {
                    TRY(set_output(self, self->state));
                    goto finally;
                }
            }
        }
        self->pulse_state = !self->pulse_state;
        TRY(set_output(self, self->pulse_state));
    }

 finally:
    return rc;
}

static void output_work_handler(struct k_work* work) {
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);
    int64_t const now = k_uptime_get();
    for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
        if (g_outputs[i]) {
            TRY_PASS(handle_output(g_outputs[i], now));
        }
    }
    k_mutex_unlock(&g_outputs_mutex);
    k_work_schedule(&g_output_work, K_MSEC(CONFIG_ZTL_DIGITAL_OUTPUT_POLL_PERIOD_MS));
}


int ztl_digital_output__init(struct ZtlDigitalOutput* const self, struct gpio_dt_spec const* const gpio) {
    int rc = ER_NO_MEM;
    bool is_first_init = false;

    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != gpio, ER_INVAL);
    ASSERT(device_is_ready(gpio->port), ER_NO_DEV);

    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
        if (g_outputs[i]) {
            ASSERT(!(g_outputs[i]->gpio->port == gpio->port && g_outputs[i]->gpio->pin == gpio->pin), ER_ALREADY);
        }
    }

    for (int i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
        if (NULL == g_outputs[i]) {
            if (i == 0 && g_outputs[0] == NULL) {
                is_first_init = true;
            }
            g_outputs[i] = self;
            memset(self, 0, sizeof(*self));
            self->gpio = gpio;
            self->pulse_period_ms = DEFAULT_PULSE_PERIOD_MS;
            self->pulse_on_ms = DEFAULT_BLINK_ON_MS;
            TRY(gpio_pin_configure_dt(self->gpio, GPIO_OUTPUT_INACTIVE));
            TRY(gpio_pin_set_dt(gpio, self->state));
            rc = 0;
            break;
        }
    }

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    if (rc == 0 && is_first_init) {
        k_work_init_delayable(&g_output_work, output_work_handler);
        k_work_schedule(&g_output_work, K_MSEC(CONFIG_ZTL_DIGITAL_OUTPUT_POLL_PERIOD_MS));
    }

    return rc;
}

int ztl_digital_output__set(struct ZtlDigitalOutput* const self, bool const state) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT(NULL != self, ER_INVAL);

    if (0 == self->pulse_count) {
        TRY(set_output(self, state));
    }

    self->state = state;

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__start_pulse(struct ZtlDigitalOutput* const self, int const pulse_count) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT(NULL != self, ER_INVAL);

    self->pulse_count = pulse_count;
    self->pulse_state = true;
    self->tl_pulse_ms = k_uptime_get();
    TRY(set_output(self, self->pulse_state));

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__config_pulse(struct ZtlDigitalOutput* const self, uint16_t const pulse_period_ms, uint16_t const pulse_on_ms) {
    int rc = 0;

    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT(NULL != self, ER_INVAL);
    ASSERT(pulse_on_ms > 0, ER_INVAL);
    ASSERT(pulse_on_ms < pulse_period_ms, ER_INVAL);

    self->pulse_period_ms = pulse_period_ms;
    self->pulse_on_ms = pulse_on_ms;

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}

int ztl_digital_output__stop_pulse(struct ZtlDigitalOutput* const self) {
    int rc = 0;
    k_mutex_lock(&g_outputs_mutex, K_FOREVER);

    ASSERT(NULL != self, ER_INVAL);

    self->pulse_count = 0;
    TRY(set_output(self, self->state));

 finally:

    k_mutex_unlock(&g_outputs_mutex);

    return rc;
}
