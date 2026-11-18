/*
 * features.c - window feature extraction (see features.h).
 *
 * Arithmetic rules (keep in sync with tools/ml/sigml/features.py and do not
 * reorder): float32 throughout, two-pass mean/std, magnitudes summed as
 * ((x*x + y*y) + z*z), divisions by n done once at the end. Built with
 * -ffp-contract=off on both target and host so no FMA changes the rounding.
 */
#include "features.h"
#include <math.h>

const char *const sig_feature_names[SIG_N_FEATURES] = {
    "acc_mean", "acc_std", "acc_min", "acc_max", "acc_dev_max",
    "jerk_mean", "jerk_max", "gyro_mean", "gyro_std", "gyro_max",
    "high_g_frac", "axis_std_max",
};

static inline float acc_mag(const sig_raw_imu_t *s)
{
    const float k = 1.0f / SIG_ACC_LSB_PER_G;       /* 2^-12, exact */
    float x = (float)s->ax * k, y = (float)s->ay * k, z = (float)s->az * k;
    return sqrtf((x * x + y * y) + z * z);
}

static inline float gyro_mag(const sig_raw_imu_t *s, const float b[3])
{
    float x = ((float)s->gx - b[0]) / SIG_GYRO_LSB_PER_DPS;
    float y = ((float)s->gy - b[1]) / SIG_GYRO_LSB_PER_DPS;
    float z = ((float)s->gz - b[2]) / SIG_GYRO_LSB_PER_DPS;
    return sqrtf((x * x + y * y) + z * z);
}

#define SAMPLE(i) (&ring[(start + (i)) % cap])

void sig_features_compute_ring(const sig_raw_imu_t *ring, int cap, int start, int n,
                               const float gyro_bias[3], float *out, float *scratch)
{
    float *amag = scratch, *wmag = scratch + n;
    const float k = 1.0f / SIG_ACC_LSB_PER_G;
    float a_sum = 0.0f, w_sum = 0.0f;
    float ax_sum = 0.0f, ay_sum = 0.0f, az_sum = 0.0f;
    float a_min = 1e30f, a_max = 0.0f, w_max = 0.0f, dev_max = 0.0f;
    float jerk_sum = 0.0f, jerk_max = 0.0f;
    int high_g = 0;
    float a_prev = 0.0f;

    /* Pass 1: sums, extrema, jerk. */
    for (int i = 0; i < n; i++) {
        const sig_raw_imu_t *s = SAMPLE(i);
        float a = acc_mag(s);
        float w = gyro_mag(s, gyro_bias);
        amag[i] = a;
        wmag[i] = w;
        float dev = fabsf(a - 1.0f);
        a_sum += a;
        w_sum += w;
        ax_sum += (float)s->ax * k;
        ay_sum += (float)s->ay * k;
        az_sum += (float)s->az * k;
        if (a < a_min) a_min = a;
        if (a > a_max) a_max = a;
        if (w > w_max) w_max = w;
        if (dev > dev_max) dev_max = dev;
        if (dev > SIG_HIGH_G_DEV) high_g++;
        if (i > 0) {
            float j = fabsf(a - a_prev) * (float)SIG_FS_HZ;
            jerk_sum += j;
            if (j > jerk_max) jerk_max = j;
        }
        a_prev = a;
    }
    const float fn = (float)n;
    const float a_mean = a_sum / fn, w_mean = w_sum / fn;
    const float mx = ax_sum / fn, my = ay_sum / fn, mz = az_sum / fn;

    /* Pass 2: deviations from the means. */
    float a_var = 0.0f, w_var = 0.0f, vx = 0.0f, vy = 0.0f, vz = 0.0f;
    for (int i = 0; i < n; i++) {
        const sig_raw_imu_t *s = SAMPLE(i);
        float da = amag[i] - a_mean;
        float dw = wmag[i] - w_mean;
        float dx = (float)s->ax * k - mx;
        float dy = (float)s->ay * k - my;
        float dz = (float)s->az * k - mz;
        a_var += da * da;
        w_var += dw * dw;
        vx += dx * dx;
        vy += dy * dy;
        vz += dz * dz;
    }
    float sx = sqrtf(vx / fn), sy = sqrtf(vy / fn), sz = sqrtf(vz / fn);
    float axis_std_max = sx;
    if (sy > axis_std_max) axis_std_max = sy;
    if (sz > axis_std_max) axis_std_max = sz;

    out[SIG_F_ACC_MEAN]     = a_mean;
    out[SIG_F_ACC_STD]      = sqrtf(a_var / fn);
    out[SIG_F_ACC_MIN]      = a_min;
    out[SIG_F_ACC_MAX]      = a_max;
    out[SIG_F_ACC_DEV_MAX]  = dev_max;
    out[SIG_F_JERK_MEAN]    = jerk_sum / (float)(n - 1);
    out[SIG_F_JERK_MAX]     = jerk_max;
    out[SIG_F_GYRO_MEAN]    = w_mean;
    out[SIG_F_GYRO_STD]     = sqrtf(w_var / fn);
    out[SIG_F_GYRO_MAX]     = w_max;
    out[SIG_F_HIGH_G_FRAC]  = (float)high_g / fn;
    out[SIG_F_AXIS_STD_MAX] = axis_std_max;
}

void sig_features_compute(const sig_raw_imu_t *win, int n,
                          const float gyro_bias[3], float *out, float *scratch)
{
    sig_features_compute_ring(win, n, 0, n, gyro_bias, out, scratch);
}
