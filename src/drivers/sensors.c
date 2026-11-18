#include "sensors.h"
#include "i2c_buses.h"
#include "board_pins.h"
#include "bsp.h"

/* ===================== ICM-20608-G ======================================== */
#define ICM_SMPLRT_DIV     0x19
#define ICM_CONFIG         0x1A
#define ICM_GYRO_CONFIG    0x1B
#define ICM_ACCEL_CONFIG   0x1C
#define ICM_ACCEL_CONFIG2  0x1D
#define ICM_INT_PIN_CFG    0x37
#define ICM_INT_ENABLE     0x38
#define ICM_ACCEL_XOUT_H   0x3B
#define ICM_PWR_MGMT_1     0x6B
#define ICM_PWR_MGMT_2     0x6C
#define ICM_WHO_AM_I       0x75

/* Register plan (datasheet DS-000081 sections 9-10):
 *  SMPLRT_DIV 9           -> 1 kHz / (1 + 9) = 100 Hz output data rate
 *  CONFIG 0x03            -> gyro DLPF 41 Hz (anti-alias below the 50 Hz Nyquist)
 *  GYRO_CONFIG 0x10       -> +/-1000 dps, 32.8 LSB/dps
 *  ACCEL_CONFIG 0x10      -> +/-8 g, 4096 LSB/g
 *  ACCEL_CONFIG2 0x03     -> accel DLPF 44.8 Hz
 *  INT_PIN_CFG 0x10       -> active high, push-pull, 50 us pulse, cleared on any read
 *  INT_ENABLE 0x01        -> DATA_RDY interrupt on GPIO_ICM_INT (PG12)               */
static const uint8_t s_icm_cfg[][2] = {
    { ICM_PWR_MGMT_1, 0x01 }, { ICM_PWR_MGMT_2, 0x00 }, { ICM_SMPLRT_DIV, 9 },
    { ICM_CONFIG, 0x03 }, { ICM_GYRO_CONFIG, 0x10 }, { ICM_ACCEL_CONFIG, 0x10 },
    { ICM_ACCEL_CONFIG2, 0x03 }, { ICM_INT_PIN_CFG, 0x10 }, { ICM_INT_ENABLE, 0x01 },
};

bool icm_init(imu_info_t *info)
{
    info->present = false;
    info->config_verified = false;
    info->whoami = 0;
    if (i2c2_write_reg(ICM20608_I2C_ADDR, ICM_PWR_MGMT_1, 0x80) != I2C_OK) {   /* device reset */
        return false;
    }
    sig_delay_ms(100);
    if (i2c2_mem_read(ICM20608_I2C_ADDR, ICM_WHO_AM_I, &info->whoami, 1) != I2C_OK) {
        return false;
    }
    info->present = (info->whoami == ICM_WHOAMI_20608G || info->whoami == ICM_WHOAMI_20608D);
    if (!info->present) return false;

    for (unsigned i = 0; i < sizeof(s_icm_cfg) / sizeof(s_icm_cfg[0]); i++) {
        if (i2c2_write_reg(ICM20608_I2C_ADDR, s_icm_cfg[i][0], s_icm_cfg[i][1]) != I2C_OK) return false;
    }
    sig_delay_ms(20);
    bool ok = true;
    for (unsigned i = 0; i < sizeof(s_icm_cfg) / sizeof(s_icm_cfg[0]); i++) {
        uint8_t v = 0;
        if (i2c2_mem_read(ICM20608_I2C_ADDR, s_icm_cfg[i][0], &v, 1) != I2C_OK || v != s_icm_cfg[i][1]) ok = false;
    }
    info->config_verified = ok;
    return ok;
}

imu_status_t icm_read(sig_raw_imu_t *o, int16_t *temp_raw)
{
    uint8_t b[14];
    if (i2c2_mem_read(ICM20608_I2C_ADDR, ICM_ACCEL_XOUT_H, b, sizeof b) != I2C_OK) return IMU_ERR_BUS;
    bool zero = true, ones = true;
    for (int i = 0; i < 14; i++) {
        if (b[i] != 0x00) zero = false;
        if (b[i] != 0xFF) ones = false;
    }
    if (zero || ones) return IMU_ERR_STUCK;
    o->ax = (int16_t)((b[0] << 8) | b[1]);
    o->ay = (int16_t)((b[2] << 8) | b[3]);
    o->az = (int16_t)((b[4] << 8) | b[5]);
    if (temp_raw) *temp_raw = (int16_t)((b[6] << 8) | b[7]);
    o->gx = (int16_t)((b[8] << 8) | b[9]);
    o->gy = (int16_t)((b[10] << 8) | b[11]);
    o->gz = (int16_t)((b[12] << 8) | b[13]);
    return IMU_OK;
}

float icm_temp_c(int16_t raw) { return (float)raw / 326.8f + 25.0f; }

/* ===================== AP3216C ============================================ */
#define AP_SYS_CONFIG  0x00
#define AP_IR_L        0x0A
#define AP_IR_H        0x0B
#define AP_ALS_L       0x0C
#define AP_ALS_H       0x0D
#define AP_PS_L        0x0E
#define AP_PS_H        0x0F

bool ap3216c_init(void)
{
    if (i2c2_write_reg(AP3216C_I2C_ADDR, AP_SYS_CONFIG, 0x04) != I2C_OK) return false;  /* SW reset */
    sig_delay_ms(15);                                                                     /* >= 10 ms */
    if (i2c2_write_reg(AP3216C_I2C_ADDR, AP_SYS_CONFIG, 0x03) != I2C_OK) return false;  /* ALS+PS+IR */
    uint8_t v = 0;
    return i2c2_mem_read(AP3216C_I2C_ADDR, AP_SYS_CONFIG, &v, 1) == I2C_OK && v == 0x03;
}

bool ap3216c_read(als_reading_t *o)
{
    uint8_t r[6];
    static const uint8_t regs[6] = { AP_IR_L, AP_IR_H, AP_ALS_L, AP_ALS_H, AP_PS_L, AP_PS_H };
    for (int i = 0; i < 6; i++) {
        if (i2c2_mem_read(AP3216C_I2C_ADDR, regs[i], &r[i], 1) != I2C_OK) return false;
    }
    o->ir = (uint16_t)((r[0] & 0x03u) | ((uint16_t)r[1] << 2));
    o->als = (uint16_t)(r[2] | ((uint16_t)r[3] << 8));
    o->ps = (uint16_t)((r[4] & 0x0Fu) | ((uint16_t)(r[5] & 0x3Fu) << 4));
    /* Bit 6 of PS_L/PS_H = IR_OF: PS data invalid under strong IR (datasheet table 13). */
    o->ps_valid = !((r[4] & 0x40u) || (r[5] & 0x40u));
    return true;
}

/* ===================== AHT21 ============================================== */
uint8_t aht21_crc8(const uint8_t *d, int n)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
    }
    return crc;
}

aht_status_t aht21_init(uint8_t *status_out)
{
    uint8_t st = 0;
    if (sw_i2c_read(AHT21_I2C_ADDR, &st, 1) != I2C_OK) return AHT_ERR_NACK;
    if (!(st & 0x08u)) {                                  /* calibration-enable bit clear */
        static const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
        if (sw_i2c_write(AHT21_I2C_ADDR, init_cmd, 3) != I2C_OK) return AHT_ERR_NACK;
        sig_delay_ms(10);
        if (sw_i2c_read(AHT21_I2C_ADDR, &st, 1) != I2C_OK) return AHT_ERR_NACK;
    }
    if (status_out) *status_out = st;
    return AHT_OK;
}

aht_status_t aht21_trigger(void)
{
    static const uint8_t cmd[3] = { 0xAC, 0x33, 0x00 };
    return sw_i2c_write(AHT21_I2C_ADDR, cmd, 3) == I2C_OK ? AHT_OK : AHT_ERR_NACK;
}

aht_status_t aht21_fetch(float *t, float *rh)
{
    uint8_t d[7];
    if (sw_i2c_read(AHT21_I2C_ADDR, d, 7) != I2C_OK) return AHT_ERR_NACK;
    if (d[0] & 0x80u) return AHT_ERR_BUSY;
    if (aht21_crc8(d, 6) != d[6]) return AHT_ERR_CRC;
    uint32_t hraw = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t traw = (((uint32_t)d[3] & 0x0Fu) << 16) | ((uint32_t)d[4] << 8) | d[5];
    float h = (float)hraw * 100.0f / 1048576.0f;
    float c = (float)traw * 200.0f / 1048576.0f - 50.0f;
    if (h < 0.0f || h > 100.0f || c < -40.0f || c > 85.0f) return AHT_ERR_RANGE;
    *rh = h;
    *t = c;
    return AHT_OK;
}
