#ifndef ZTL_DIGITAL_OUTPUT_H_
#define ZTL_DIGITAL_OUTPUT_H_

#include "digital_common.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/mutex.h>

typedef struct ZtlDigitalOutput {
    struct gpio_dt_spec const* gpio;
    bool state;
    bool hw_state;
    int32_t pulse_count;
    uint16_t pulse_period_ms;
    uint16_t pulse_on_ms;
    uint64_t tl_pulse_ms;
    bool pulse_state;
} ZtlDigitalOutput;

int ztl_digital_output__init(struct ZtlDigitalOutput* self, struct gpio_dt_spec const* gpio);
int ztl_digital_output__set(struct ZtlDigitalOutput* self, bool state);
int ztl_digital_output__start_pulse(struct ZtlDigitalOutput* self, int32_t pulse_count);
int ztl_digital_output__config_pulse(struct ZtlDigitalOutput* self, uint16_t pulse_period_ms, uint16_t pulse_on_ms);
int ztl_digital_output__stop_pulse(struct ZtlDigitalOutput* self);
int ztl_digital_output__is_pulse_run(struct ZtlDigitalOutput* self, bool* is_running);
int ztl_digital_output__wait_pulse_end(struct ZtlDigitalOutput* self);

#endif // ZTL_DIGITAL_OUTPUT_H_
