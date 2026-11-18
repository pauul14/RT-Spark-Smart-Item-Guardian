/*
 * sensors.h - ICM-20608-G (IMU), AP3216C (ALS/PS), AHT21 (T/RH) drivers.
 * Register maps: TDK ICM-20608-G datasheet DS-000081, Dyna-Image AP3216C
 * datasheet, Aosong AHT21 datasheet; cross-checked against the RT-Thread
 * packages icm20608/ap3216c/aht10 (Apache-2.0) used by the vendor BSP.
 */
#ifndef SIG_SENSORS_H
#define SIG_SENSORS_H

#include <stdbool.h>
#include <stdint.h>
#include "sig_types.h"

/* ---- ICM-20608-G --------------------------------------------------------- */
#define ICM_WHOAMI_20608G  0xAFu
#define ICM_WHOAMI_20608D  0xAEu

typedef enum {
    IMU_OK = 0,
    IMU_ERR_BUS,          /* I2C error/NACK */
    IMU_ERR_STUCK,        /* all-zero frame (sensor reset/asleep) or all 0xFF (bus floating) */
} imu_status_t;

typedef struct {
    uint8_t whoami;
    bool    present;
    bool    config_verified;  /* register read-back matched */
} imu_info_t;

bool         icm_init(imu_info_t *info);           /* 100 Hz ODR, +/-8 g, +/-1000 dps, DRDY int */
imu_status_t icm_read(sig_raw_imu_t *out, int16_t *temp_raw);
float        icm_temp_c(int16_t raw);               /* (raw / 326.8) + 25 */

/* ---- AP3216C ------------------------------------------------------------- */
typedef struct {
    uint16_t als;        /* raw counts (range 0: 0.35 lx/count) */
    uint16_t ps;         /* 10-bit proximity counts */
    uint16_t ir;         /* 10-bit IR counts */
    bool     ps_valid;   /* false when IR overflow flags are set */
} als_reading_t;

bool ap3216c_init(void);
bool ap3216c_read(als_reading_t *out);
static inline float ap3216c_lux(uint16_t als) { return (float)als * 0.35f; }

/* ---- AHT21 (soft I2C) ---------------------------------------------------- */
typedef enum { AHT_OK = 0, AHT_ERR_NACK, AHT_ERR_BUSY, AHT_ERR_CRC, AHT_ERR_RANGE } aht_status_t;

aht_status_t aht21_init(uint8_t *status_out);
aht_status_t aht21_trigger(void);                    /* then wait >= 80 ms */
aht_status_t aht21_fetch(float *temp_c, float *rh_pct);
uint8_t      aht21_crc8(const uint8_t *d, int n);    /* poly 0x31, init 0xFF (exported for tests) */

#endif
