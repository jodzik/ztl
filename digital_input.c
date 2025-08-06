#include "digital_input.h"
#include "time.h"

#include <lib/safe-c/safe_c.h>

#include <zephyr/kernel.h>

enum {
    DEFAULT_DEBOUNCE_DURATION_MS = 100,
    THREAD_STACK_SIZE = 512,
    THREAD_LOOP_SLEEP_US = 1000,
    THREAD_PRIO = 3,
};

LOG_MODULE_REGISTER(ztl_digital_input);

static struct ZtlDigitalInput* g_inputs[CONFIG_ZTL_DIGITAL_INPUT_MAX_COUNT] = {0};
K_MUTEX_DEFINE(g_inputs_mutex);

static void input_handler(void*, void*, void*);

K_THREAD_DEFINE(input_handler_tid, THREAD_STACK_SIZE,
                input_handler, NULL, NULL, NULL,
                THREAD_PRIO, 0, 0);

static inline void call_subs(
    struct ZtlDigitalInput* const self,
    struct ZtlDigitalInputCallbackDescriptor const* const cb_descr,
    enum ZtlDigitalInputEventType const event)
{
    k_mutex_unlock(&g_inputs_mutex);
    cb_descr->callback(event, cb_descr->arg);
    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
}

static void handle_input(struct ZtlDigitalInput* const self, uint64_t const now) {
    bool const new_state = (bool)gpio_pin_get_dt(self->gpio);
    self->tl_handling = now;
    if (new_state != self->prev_state) {
        // Handle just state change
        self->tl_state_change = now;
        self->is_state_changed = true;
        self->is_subs_called_for_duration = false;
        // Call all subs on state change
        for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_SUBSCRIBERS_COUNT; i++) {
            struct ZtlDigitalInputCallbackDescriptor const* const cb_descr = &self->callback_descriptors[i];
            if (cb_descr->callback) {
                if (new_state && cb_descr->conditions.change_state_to_active) {
                    call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_ACTIVE);
                } else if (!new_state && cb_descr->conditions.change_state_to_inactive) {
                    call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_INACTIVE);
                }
            }
        }
        self->prev_state = new_state;
    } else {
        // Handle debounced state change
        uint64_t const level_duration = now - self->tl_state_change;
        if (level_duration >= self->debounce_duration_ms) {
            if (self->prev_state_debounced != self->prev_state) {
                self->prev_state_debounced = self->prev_state;
                self->is_state_changed_debounced = true;
                self->is_state_changed_debounced_button = true;
                // Call all subs on debounced state change
                for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_SUBSCRIBERS_COUNT; i++) {
                    struct ZtlDigitalInputCallbackDescriptor const* const cb_descr = &self->callback_descriptors[i];
                    if (cb_descr->callback) {
                        if (self->prev_state && cb_descr->conditions.change_state_to_active_debounced) {
                            call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_ACTIVE_DEBOUNCED);
                        } else if (!self->prev_state && cb_descr->conditions.change_state_to_inactive_debounced) {
                            call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_INACTIVE_DEBOUNCED);
                        }
                    }
                }
                self->prev_state_debounced = self->prev_state;
            }
        }

        // Call all subs on state duration
        for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_SUBSCRIBERS_COUNT; i++) {
            struct ZtlDigitalInputCallbackDescriptor const* const cb_descr = &self->callback_descriptors[i];
            if (cb_descr->callback) {
                uint32_t const active_dur_cond = cb_descr->conditions.active_state_duration;
                uint32_t const inactive_dur_cond = cb_descr->conditions.inactive_state_duration;
                bool const is_active_duration_check = self->prev_state && active_dur_cond &&
                    self->is_subs_called_for_duration;
                bool const is_inactive_duration_check = !self->prev_state && inactive_dur_cond &&
                    self->is_subs_called_for_duration;

                if (is_active_duration_check && level_duration >= active_dur_cond) {
                    call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__ACTIVE_DURATION);
                    self->is_subs_called_for_duration = true;
                } else if (is_inactive_duration_check && level_duration >= inactive_dur_cond) {
                    call_subs(self, cb_descr, ZTL_DIGITAL_INPUT_EVENT_TYPE__INACTIVE_DURATION);
                    self->is_subs_called_for_duration = true;
                }
            }
        }
    }
}

static void input_handler(void* arg1, void* arg2, void* arg3) {
    while (true) {
        k_mutex_lock(&g_inputs_mutex, K_FOREVER);
        uint64_t const now = (uint64_t)k_uptime_get();
        for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_OUTPUT_MAX_COUNT; i++) {
            if (g_inputs[i] && g_inputs[i]->tl_handling != now) {
                handle_input(g_inputs[i], now);
            }
        }
        k_mutex_unlock(&g_inputs_mutex);
        k_usleep(THREAD_LOOP_SLEEP_US);
    }
}

static void handle_if_needed(struct ZtlDigitalInput* const self) {
    uint64_t const now = (uint64_t)k_uptime_get();
    if (self->tl_handling != now) {
        handle_input(self, now);
    }
}

int ztl_digital_input__init(struct ZtlDigitalInput* const self, struct gpio_dt_spec const* gpio) {
    int rc = ER_NO_MEM;

    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != gpio, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);

    for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_COUNT; i++) {
        if (g_inputs[i]) {
            ASSERT_EX(!(g_inputs[i]->gpio->port == gpio->port && g_inputs[i]->gpio->pin == gpio->pin), ER_ALREADY);
        }
    }

    for (int i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_COUNT; i++) {
        if (NULL == g_inputs[i]) {
            gpio_flags_t gpio_cfg = 0;
            g_inputs[i] = self;
            memset(self, 0, sizeof(*self));
            self->gpio = gpio;
            self->debounce_duration_ms = DEFAULT_DEBOUNCE_DURATION_MS;
            TRY_EX(gpio_pin_get_config_dt(self->gpio, &gpio_cfg));
            if (gpio_cfg & GPIO_ACTIVE_HIGH) {
                self->active_level = ZTL_LEVEL__HIGH;
            } else {
                self->active_level = ZTL_LEVEL__LOW;
            }
            TRY_EX(gpio_pin_configure_dt(self->gpio, GPIO_INPUT));
            rc = 0;
            break;
        }
    }

 finally:

    k_mutex_unlock(&g_inputs_mutex);

    return rc;
}

int ztl_digital_input__state(struct ZtlDigitalInput* self, bool* state) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);
    *state = self->prev_state;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__wait_state(struct ZtlDigitalInput* self, bool const state) {
    ASSERT(NULL != self, ER_INVAL);

    while (true) {
        k_mutex_lock(&g_inputs_mutex, K_FOREVER);
        handle_if_needed(self);
        if (state == self->prev_state) {
            k_mutex_unlock(&g_inputs_mutex);
            break;
        }
        k_mutex_unlock(&g_inputs_mutex);
        k_usleep(THREAD_LOOP_SLEEP_US);
    }

    return 0;
}

int ztl_digital_input__is_state_changed(struct ZtlDigitalInput* self, bool* is_changed, bool* state) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != is_changed, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);
    *is_changed = self->is_state_changed;
    self->is_state_changed = false;
    *state = self->prev_state;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__state_duration(struct ZtlDigitalInput* const self, bool* state, uint64_t* duration_ms) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);
    ASSERT(NULL != duration_ms, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);
    *state = self->prev_state;
    *duration_ms = (uint64_t)k_uptime_get() - self->tl_state_change;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__state_debounced(struct ZtlDigitalInput* self, bool* state) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);
    *state = self->prev_state_debounced;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__state_button(struct ZtlDigitalInput* self, enum ZtlButtonState* state) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);

    *state = ZTL_BUTTON_STATE__NONE;

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);

    if (self->is_state_changed_debounced_button && self->prev_state_debounced) {
        *state = ZTL_BUTTON_STATE__PUSHED;
        self->is_state_changed_debounced_button = false;
    } else if (self->prev_state) {
        uint64_t const state_dur = (uint64_t)k_uptime_get() - self->tl_state_change;
        if (state_dur >= self->clump_duration_ms) {
            *state = ZTL_BUTTON_STATE__CLUMPED;
        }
    }

    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__wait_state_debounced(struct ZtlDigitalInput* self, bool const state) {
    ASSERT(NULL != self, ER_INVAL);

    while (true) {
        k_mutex_lock(&g_inputs_mutex, K_FOREVER);
        handle_if_needed(self);
        if (state == self->prev_state_debounced) {
            k_mutex_unlock(&g_inputs_mutex);
            break;
        }
        k_mutex_unlock(&g_inputs_mutex);
        k_usleep(THREAD_LOOP_SLEEP_US);
    }

    return 0;
}

int ztl_digital_input__is_state_changed_debounced(struct ZtlDigitalInput* self, bool* is_changed, bool* state) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != is_changed, ER_INVAL);
    ASSERT(NULL != state, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    handle_if_needed(self);
    *is_changed = self->is_state_changed_debounced;
    self->is_state_changed_debounced = false;
    *state = self->prev_state_debounced;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__set_debounce_duration(struct ZtlDigitalInput* self, uint16_t ms) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(ms > 0, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    self->debounce_duration_ms = ms;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__set_clump_duration(struct ZtlDigitalInput* self, uint16_t ms) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(ms > 0, ER_INVAL);

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    self->clump_duration_ms = ms;
    k_mutex_unlock(&g_inputs_mutex);

    return 0;
}

int ztl_digital_input__state_to_level(struct ZtlDigitalInput const* self, bool state, enum ZtlLevel* level) {
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != level, ER_INVAL);

    if (state) {
        *level = self->active_level;
    } else {
        *level = ztl_digital__invert_level(self->active_level);
    }

    return 0;
}

int ztl_digital_input__subscribe(
    struct ZtlDigitalInput* const self,
    struct ZtlDigitalInputEventConditions const* const conditions,
    ZtlDigitalInputCallback const cb,
    void* arg)
{
    int rc = 0;
    ASSERT(NULL != self, ER_INVAL);
    ASSERT(NULL != conditions, ER_INVAL);
    bool is_found_free = false;

    k_mutex_lock(&g_inputs_mutex, K_FOREVER);
    for (uint8_t i = 0; i < CONFIG_ZTL_DIGITAL_INPUT_MAX_SUBSCRIBERS_COUNT; i++) {
        if (NULL == self->callback_descriptors[i].callback || cb == self->callback_descriptors[i].callback) {
            self->callback_descriptors[i].callback = cb;
            self->callback_descriptors[i].conditions = *conditions;
            self->callback_descriptors[i].arg = arg;
            is_found_free = true;
            break;
        }
    }

    ASSERT_EX(is_found_free, ER_NO_MEM);

 finally:

    k_mutex_unlock(&g_inputs_mutex);

    return rc;
}

int ztl_digital_input__subscribe_to_state_change(
    struct ZtlDigitalInput* self,
    bool to_active,
    bool to_inactive,
    ZtlDigitalInputCallback cb,
    void* arg)
{
    struct ZtlDigitalInputEventConditions const cond = {
        .change_state_to_active = to_active,
        .change_state_to_inactive = to_inactive,
        .change_state_to_active_debounced = false,
        .change_state_to_inactive_debounced = false,
        .active_state_duration = 0,
        .inactive_state_duration = 0,
    };

    return ztl_digital_input__subscribe(self, &cond, cb, arg);
}

int ztl_digital_input__subscribe_to_state_change_debounced(
    struct ZtlDigitalInput* self,
    bool to_active,
    bool to_inactive,
    ZtlDigitalInputCallback cb,
    void* arg)
{
    struct ZtlDigitalInputEventConditions const cond = {
        .change_state_to_active = false,
        .change_state_to_inactive = false,
        .change_state_to_active_debounced = to_active,
        .change_state_to_inactive_debounced = to_inactive,
        .active_state_duration = 0,
        .inactive_state_duration = 0,
    };

    return ztl_digital_input__subscribe(self, &cond, cb, arg);
}

int ztl_digital_input__subscribe_to_state_duration(
    struct ZtlDigitalInput* self,
    uint32_t active_duration_ms,
    uint32_t inactive_duration_ms,
    ZtlDigitalInputCallback cb,
    void* arg)
{
    struct ZtlDigitalInputEventConditions const cond = {
        .change_state_to_active = false,
        .change_state_to_inactive = false,
        .change_state_to_active_debounced = false,
        .change_state_to_inactive_debounced = false,
        .active_state_duration = active_duration_ms,
        .inactive_state_duration = inactive_duration_ms,
    };

    return ztl_digital_input__subscribe(self, &cond, cb, arg);
}
