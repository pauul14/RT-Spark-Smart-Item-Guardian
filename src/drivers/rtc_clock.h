/*
 * rtc_clock.h - RTC on LSE (32.768 kHz crystal X1), backup registers.
 * The backup domain may already be configured by earlier firmware; it is only
 * (re)initialised when it is not running from a ready LSE, so an existing
 * calendar is preserved. Time is set explicitly with the `time` console command.
 */
#ifndef SIG_RTC_CLOCK_H
#define SIG_RTC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { RTC_SRC_NONE = 0, RTC_SRC_LSE, RTC_SRC_LSI } rtc_src_t;

typedef struct { uint16_t year; uint8_t mon, day, hour, min, sec; } rtc_time_t;

/* Backup register map (RTC_BKP0R..BKP19R, 20 x 32 bit) */
#define BKP_MAGIC        0     /* 0x5165A001 when this firmware owns the map */
#define BKP_PROX_FAR     1     /* proximity calibration: far_mean | far_max << 16 */
#define BKP_PROX_NEAR    2     /* near_min | near_mean << 16 */
#define BKP_PROX_THR     3     /* thr_far | thr_near << 12 | version << 24 | 0xC << 28 (valid tag) */
#define BKP_FLAGS        4     /* option bits, see FLAG_* in src/app/app.h */
#define BKP_IR_TEST      5     /* paired remote codes: valid | addr << 8 | cmd */
#define BKP_IR_ACK       6
#define BKP_IR_NEXT      7
#define BKP_MAGIC_VALUE  0x5165A001u

rtc_src_t rtc_init(bool *was_running);
bool      rtc_get(rtc_time_t *t);
bool      rtc_set(const rtc_time_t *t);
bool      rtc_time_valid(void);            /* calendar set by this firmware since last backup reset */
uint32_t  bkp_read(uint32_t idx);
void      bkp_write(uint32_t idx, uint32_t v);
const char *rtc_src_name(rtc_src_t s);

#endif
