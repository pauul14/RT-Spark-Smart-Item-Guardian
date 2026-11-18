/*
 * i2c_buses.h
 *  - I2C2 (hardware, PF1/PF0, 400 kHz): ICM-20608-G, AP3216C, ES8388.
 *    Calls are serialised by a FreeRTOS mutex once i2c2_rtos_init() ran; a bus
 *    error or timeout triggers the 9-clock recovery sequence.
 *  - Software I2C on PE0/PE1 (~100 kHz) for the AHT21 (no AF mapping exists there).
 */
#ifndef SIG_I2C_BUSES_H
#define SIG_I2C_BUSES_H

#include <stdbool.h>
#include <stdint.h>

#define I2C_OK        0
#define I2C_ERR_NACK -1
#define I2C_ERR_BUS  -2
#define I2C_ERR_LOCK -3

bool i2c2_init(void);
void i2c2_rtos_init(void);
int  i2c2_mem_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t n);
int  i2c2_mem_write(uint8_t addr7, uint8_t reg, const uint8_t *buf, uint16_t n);
int  i2c2_write_reg(uint8_t addr7, uint8_t reg, uint8_t val);
bool i2c2_probe(uint8_t addr7);
void i2c2_recover(void);

extern volatile uint32_t g_i2c2_errors;
extern volatile uint32_t g_i2c2_recoveries;

/* Software I2C (AHT21 bus). Transactions are atomic per call; no mutex is
 * needed because only the health task uses this bus. */
void sw_i2c_init(void);
int  sw_i2c_write(uint8_t addr7, const uint8_t *data, uint16_t n);
int  sw_i2c_read(uint8_t addr7, uint8_t *data, uint16_t n);
extern volatile uint32_t g_swi2c_errors;

#endif
