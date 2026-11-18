/*
 * prox_ack.h - touchless alert acknowledgement from the AP3216C proximity
 * channel (portable, unit-tested on host).
 *
 * Deliberate activation. A gesture is reported only when
 *   1. the sensor has seen FAR for PROX_REARM_SAMPLES consecutive valid readings
 *      (the detector is then "ready"), and
 *   2. a hand comes NEAR and stays NEAR for PROX_HOLD_SAMPLES consecutive valid
 *      readings (5 Hz -> 0.6 s hold).
 * After a gesture the detector is latched until FAR has been seen again for
 * PROX_REARM_SAMPLES readings. Consequences:
 *   - a hand or object that stays near triggers at most once;
 *   - something already near at power-up or after calibration never triggers;
 *   - a brief pass (fewer than PROX_HOLD_SAMPLES near readings) never triggers.
 * NEAR / FAR use two thresholds (hysteresis); a reading between them keeps the
 * previous zone. Invalid readings (I2C error, IR overflow flag) are counted and
 * ignored; they neither advance nor reset a streak.
 *
 * Thresholds come only from a measured calibration (prox_cal_from_stats: hand
 * away, then hand held over the sensor). Until then the detector is disabled -
 * there are no invented default thresholds. Whether a gesture acknowledges an
 * alert is decided by the product logic (gl_prox_ack in guardian_logic.h).
 */
#ifndef SIG_PROX_ACK_H
#define SIG_PROX_ACK_H

#include "sig_types.h"

#define PROX_HOLD_SAMPLES    3     /* consecutive NEAR readings for a gesture (0.6 s at 5 Hz) */
#define PROX_REARM_SAMPLES   3     /* consecutive FAR readings before a (new) gesture can start */
#define PROX_MIN_GAP         20    /* counts between far max and near min for a valid calibration */
#define PROX_MIN_CAL_SAMPLES 5     /* valid readings needed per calibration phase */

typedef struct {
    bool     calibrated;
    uint16_t far_mean, far_max;    /* hand away (calibration phase 1), raw 10-bit PS counts */
    uint16_t near_min, near_mean;  /* hand held over the sensor (phase 2) */
    uint16_t thr_far, thr_near;    /* PS <= thr_far: FAR; PS >= thr_near: NEAR */
} prox_cal_t;

typedef enum { PROX_ZONE_UNKNOWN = 0, PROX_ZONE_FAR, PROX_ZONE_NEAR } prox_zone_t;

typedef enum {
    PROX_CAL_OK = 0,
    PROX_CAL_FAIL_NO_SAMPLES,      /* fewer than PROX_MIN_CAL_SAMPLES valid readings in a phase */
    PROX_CAL_FAIL_LOW_CONTRAST,    /* near_min - far_max < PROX_MIN_GAP */
} prox_cal_result_t;

typedef struct {
    prox_cal_t  cal;
    prox_zone_t zone;
    uint8_t     far_streak, near_streak;
    bool        ready;             /* FAR seen long enough: a NEAR hold may fire */
    bool        latched;           /* fired; waiting for FAR */
    uint32_t    gestures, invalid_readings;
} prox_det_t;

void prox_cal_clear(prox_cal_t *c);
/* Build a calibration from the two measured phases. On failure *c is unchanged. */
prox_cal_result_t prox_cal_from_stats(prox_cal_t *c, uint16_t far_mean, uint16_t far_max, uint32_t n_far,
                                      uint16_t near_min, uint16_t near_mean, uint32_t n_near);
void prox_init(prox_det_t *d, const prox_cal_t *cal);
/* Feed one reading (5 Hz). Returns true exactly once per deliberate gesture. */
bool prox_update(prox_det_t *d, uint16_t ps, bool valid);
const char *prox_zone_name(prox_zone_t z);
const char *prox_cal_result_name(prox_cal_result_t r);

#endif /* SIG_PROX_ACK_H */
