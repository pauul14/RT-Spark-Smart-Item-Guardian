/*
 * sig_types.h - portable types shared by firmware and host verification.
 * No HAL / RTOS dependencies: this header and everything in src/core compile
 * unchanged with the host GCC (tests/host, tools/ml/verify_c_model.py).
 */
#ifndef SIG_TYPES_H
#define SIG_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Handling classes predicted by the model (order is part of the model ABI). */
typedef enum {
    SIG_CLASS_STATIONARY = 0,
    SIG_CLASS_NORMAL     = 1,
    SIG_CLASS_ROUGH      = 2,
    SIG_CLASS_COUNT      = 3,
    SIG_CLASS_UNKNOWN    = 255   /* invalid window: no prediction is made */
} sig_class_t;

/* One IMU sample as delivered by the ICM-20608-G (raw two's-complement counts). */
typedef struct {
    int16_t ax, ay, az;   /* accelerometer, +/-8 g range  -> 4096 LSB/g   */
    int16_t gx, gy, gz;   /* gyroscope,  +/-1000 dps range -> 32.8 LSB/dps */
} sig_raw_imu_t;

static inline const char *sig_class_name(uint8_t c)
{
    switch (c) {
    case SIG_CLASS_STATIONARY: return "STATIONARY";
    case SIG_CLASS_NORMAL:     return "NORMAL_HANDLING";
    case SIG_CLASS_ROUGH:      return "ROUGH_HANDLING";
    default:                   return "UNKNOWN";
    }
}

static inline const char *sig_class_short(uint8_t c)
{
    switch (c) {
    case SIG_CLASS_STATIONARY: return "STILL";
    case SIG_CLASS_NORMAL:     return "NORMAL";
    case SIG_CLASS_ROUGH:      return "ROUGH";
    default:                   return "UNKNOWN";
    }
}

#endif /* SIG_TYPES_H */
