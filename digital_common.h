#ifndef ZTL_DIGITAL_COMMON_H_
#define ZTL_DIGITAL_COMMON_H_

typedef enum ZtlLevel {
    ZTL_LEVEL__LOW = 0,
    ZTL_LEVEL__HIGH = 1,
} ZtlLevel;

typedef enum ZtlDigitalState {
    ZTL_DIGITAL_STATE_UNACTIVE = 0,
    ZTL_DIGITAL_STATE__ACTIVE = 1,
} ZtlDigitalState;

static inline enum ZtlLevel ztl_digital__invert_level(enum ZtlLevel const level) {
    if (ZTL_LEVEL__LOW == level) {
        return ZTL_LEVEL__HIGH;
    } else {
        return ZTL_LEVEL__LOW;
    }
}

#endif // ZTL_DIGITAL_COMMON_H_