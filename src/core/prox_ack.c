/* prox_ack.c - see prox_ack.h. */
#include "prox_ack.h"
#include <string.h>

void prox_cal_clear(prox_cal_t *c)
{
    memset(c, 0, sizeof(*c));
    c->calibrated = false;
}

prox_cal_result_t prox_cal_from_stats(prox_cal_t *c, uint16_t far_mean, uint16_t far_max, uint32_t n_far,
                                      uint16_t near_min, uint16_t near_mean, uint32_t n_near)
{
    if (n_far < PROX_MIN_CAL_SAMPLES || n_near < PROX_MIN_CAL_SAMPLES) return PROX_CAL_FAIL_NO_SAMPLES;
    if ((int)near_min - (int)far_max < PROX_MIN_GAP) return PROX_CAL_FAIL_LOW_CONTRAST;
    uint16_t gap = (uint16_t)(near_min - far_max);
    prox_cal_t n;
    memset(&n, 0, sizeof(n));
    n.far_mean = far_mean;
    n.far_max = far_max;
    n.near_min = near_min;
    n.near_mean = near_mean;
    n.thr_far = (uint16_t)(far_max + gap / 4u);    /* release: well below the weakest "near" reading */
    n.thr_near = (uint16_t)(far_max + gap / 2u);   /* trigger: halfway between far max and near min */
    n.calibrated = true;
    *c = n;
    return PROX_CAL_OK;
}

void prox_init(prox_det_t *d, const prox_cal_t *cal)
{
    memset(d, 0, sizeof(*d));
    d->cal = *cal;
    d->zone = PROX_ZONE_UNKNOWN;
}

bool prox_update(prox_det_t *d, uint16_t ps, bool valid)
{
    if (!d->cal.calibrated) return false;
    if (!valid) {
        d->invalid_readings++;
        return false;
    }
    prox_zone_t z = ps >= d->cal.thr_near ? PROX_ZONE_NEAR : (ps <= d->cal.thr_far ? PROX_ZONE_FAR : d->zone);
    if (z == PROX_ZONE_UNKNOWN) return false;          /* inside the band before any decision */
    d->zone = z;
    if (z == PROX_ZONE_FAR) {
        d->near_streak = 0;
        if (d->far_streak < 255) d->far_streak++;
        if (d->far_streak >= PROX_REARM_SAMPLES) {
            d->ready = true;
            d->latched = false;
        }
        return false;
    }
    d->far_streak = 0;
    if (d->latched || !d->ready) return false;
    if (++d->near_streak < PROX_HOLD_SAMPLES) return false;
    d->near_streak = 0;
    d->latched = true;
    d->ready = false;
    d->gestures++;
    return true;
}

const char *prox_zone_name(prox_zone_t z)
{
    switch (z) {
    case PROX_ZONE_FAR:  return "FAR";
    case PROX_ZONE_NEAR: return "NEAR";
    default:             return "UNKNOWN";
    }
}

const char *prox_cal_result_name(prox_cal_result_t r)
{
    switch (r) {
    case PROX_CAL_OK:                return "ok";
    case PROX_CAL_FAIL_NO_SAMPLES:   return "fail_no_samples";
    case PROX_CAL_FAIL_LOW_CONTRAST: return "fail_low_contrast";
    default:                         return "?";
    }
}
