/*
 * features.h - window feature extraction (DSP) for handling classification.
 *
 * The Python mirror is tools/ml/sigml/features.py. The feature ORDER and the
 * preprocessing constants are part of the exported model ABI: the generated
 * src/model/model_config.h repeats them and features.c static-asserts against it.
 */
#ifndef SIG_FEATURES_H
#define SIG_FEATURES_H

#include "sig_types.h"

#define SIG_FS_HZ             100      /* IMU output data rate (SMPLRT_DIV = 9 @ 1 kHz) */
#define SIG_WINDOW_SAMPLES    200      /* 2.00 s window */
#define SIG_HOP_SAMPLES       50       /* 0.50 s between on-line inferences */
#define SIG_ACC_LSB_PER_G     4096.0f  /* ACCEL_FS_SEL = 2 (+/-8 g) */
#define SIG_GYRO_LSB_PER_DPS  32.8f    /* FS_SEL = 2 (+/-1000 dps) */
#define SIG_HIGH_G_DEV        0.40f    /* |a| deviation (g) counted as a high-g sample */

enum {
    SIG_F_ACC_MEAN = 0,   /* mean |a|                       (g)      */
    SIG_F_ACC_STD,        /* std  |a|                       (g)      */
    SIG_F_ACC_MIN,        /* min  |a|                       (g)      */
    SIG_F_ACC_MAX,        /* max  |a|                       (g)      */
    SIG_F_ACC_DEV_MAX,    /* max | |a| - 1 g |              (g)      */
    SIG_F_JERK_MEAN,      /* mean | d|a|/dt |               (g/s)    */
    SIG_F_JERK_MAX,       /* max  | d|a|/dt |               (g/s)    */
    SIG_F_GYRO_MEAN,      /* mean |w| after bias removal    (dps)    */
    SIG_F_GYRO_STD,       /* std  |w|                       (dps)    */
    SIG_F_GYRO_MAX,       /* max  |w|                       (dps)    */
    SIG_F_HIGH_G_FRAC,    /* fraction of samples with dev > SIG_HIGH_G_DEV */
    SIG_F_AXIS_STD_MAX,   /* max over x/y/z of std(a_axis)  (g)      */
    SIG_N_FEATURES
};

extern const char *const sig_feature_names[SIG_N_FEATURES];

/*
 * Compute the feature vector of one window.
 *   win        : n consecutive samples (oldest first), n >= 2
 *   gyro_bias  : per-axis gyro offset in raw LSB (subtracted before scaling)
 *   out        : SIG_N_FEATURES floats
 * Pure function; float32 arithmetic in a fixed order (see features.c).
 */
void sig_features_compute(const sig_raw_imu_t *win, int n,
                          const float gyro_bias[3], float *out, float *scratch);

/* Same as above for a ring buffer: logical sample i is ring[(start + i) % cap].
 * scratch: caller-owned, >= 2*n floats (pass-1 magnitudes reused by pass 2, so each
 * square root is computed once; results are bit-identical to recomputing them). */
#define SIG_FEATURE_SCRATCH(n) (2 * (n))
void sig_features_compute_ring(const sig_raw_imu_t *ring, int cap, int start, int n,
                               const float gyro_bias[3], float *out, float *scratch);

#endif /* SIG_FEATURES_H */
