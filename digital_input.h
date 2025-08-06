#ifndef ZTL_DIGITAL_INPUT_H_
#define ZTL_DIGITAL_INPUT_H_

#include "digital_common.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/autoconf.h>
#include <zephyr/sys/mutex.h>
#include <zephyr/types.h>

typedef enum ZtlDigitalInputPull {
    ZTL_DIGITAL_INPUT_PULL__NONE = 0,
    ZTL_DIGITAL_INPUT_PULL__UP = 1,
    ZTL_DIGITAL_INPUT_PULL__DOWN = 2,
} ZtlDigitalInputPull;

typedef enum ZtlDigitalInputEventType {
    ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_ACTIVE,
    ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_INACTIVE,
    ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_ACTIVE_DEBOUNCED,
    ZTL_DIGITAL_INPUT_EVENT_TYPE__CHANGE_STATE_TO_INACTIVE_DEBOUNCED,
    ZTL_DIGITAL_INPUT_EVENT_TYPE__ACTIVE_DURATION,
    ZTL_DIGITAL_INPUT_EVENT_TYPE__INACTIVE_DURATION,
} ZtlDigitalInputEventType;

typedef enum ZtlButtonState {
    ZTL_BUTTON_STATE__NONE = 0,
    ZTL_BUTTON_STATE__PUSHED = 1,
    ZTL_BUTTON_STATE__CLUMPED = 2,
} ZtlButtonState;

typedef void (*ZtlDigitalInputCallback)(enum ZtlDigitalInputEventType, void*);

typedef struct ZtlDigitalInputEventConditions {
    bool change_state_to_active;
    bool change_state_to_inactive;
    bool change_state_to_active_debounced;
    bool change_state_to_inactive_debounced;
    uint32_t active_state_duration;
    uint32_t inactive_state_duration;
} ZtlDigitalInputEventConditions;

typedef struct ZtlDigitalInputCallbackDescriptor {
    ZtlDigitalInputCallback callback;
    struct ZtlDigitalInputEventConditions conditions;
    void* arg;
} ZtlButtonCallbackDescriptor;

typedef struct ZtlDigitalInput {
    struct gpio_dt_spec const* gpio;
    uint16_t debounce_duration_ms;
    uint16_t clump_duration_ms;
    enum ZtlLevel active_level;
    struct ZtlDigitalInputCallbackDescriptor callback_descriptors[CONFIG_ZTL_DIGITAL_INPUT_MAX_SUBSCRIBERS_COUNT];

    bool prev_state;
    bool prev_state_debounced;
    bool is_state_changed;
    bool is_state_changed_debounced;
    bool is_state_changed_debounced_button;
    uint64_t tl_state_change;
    uint64_t tl_handling;
    bool is_subs_called_for_duration;
} ZtlDigitalInput;

int ztl_digital_input__init(struct ZtlDigitalInput* self, struct gpio_dt_spec const* gpio);
int ztl_digital_input__state(struct ZtlDigitalInput* self, bool* state);
int ztl_digital_input__wait_state(struct ZtlDigitalInput* self, bool state);
int ztl_digital_input__is_state_changed(struct ZtlDigitalInput* self, bool* is_changed, bool* state);
int ztl_digital_input__state_duration(struct ZtlDigitalInput* const self, bool* state, uint64_t* duration_ms);
int ztl_digital_input__state_debounced(struct ZtlDigitalInput* self, bool* state);
int ztl_digital_input__state_button(struct ZtlDigitalInput* self, enum ZtlButtonState* state);
int ztl_digital_input__wait_state_debounced(struct ZtlDigitalInput* self, bool state);
int ztl_digital_input__is_state_changed_debounced(struct ZtlDigitalInput* self, bool* is_changed, bool* state);
int ztl_digital_input__set_debounce_duration(struct ZtlDigitalInput* self, uint16_t ms);
int ztl_digital_input__set_clump_duration(struct ZtlDigitalInput* self, uint16_t ms);
int ztl_digital_input__state_to_level(struct ZtlDigitalInput const* self, bool state, enum ZtlLevel* level);

int ztl_digital_input__subscribe(
    struct ZtlDigitalInput* self,
    struct ZtlDigitalInputEventConditions const* conditions,
    ZtlDigitalInputCallback cb,
    void* arg);

int ztl_digital_input__subscribe_to_state_change(
    struct ZtlDigitalInput* self,
    bool to_active,
    bool to_inactive,
    ZtlDigitalInputCallback cb,
    void* arg);

int ztl_digital_input__subscribe_to_state_change_debounced(
    struct ZtlDigitalInput* self,
    bool to_active,
    bool to_inactive,
    ZtlDigitalInputCallback cb,
    void* arg);

int ztl_digital_input__subscribe_to_state_duration(
    struct ZtlDigitalInput* self,
    uint32_t active_duration_ms,
    uint32_t inactive_duration_ms,
    ZtlDigitalInputCallback cb,
    void* arg);

#endif // ZTL_DIGITAL_INPUT_H_
