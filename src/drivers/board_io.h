/*
 * board_io.h - LEDs, buzzer, arrow keys, and the EXTI dispatch.
 */
#ifndef SIG_BOARD_IO_H
#define SIG_BOARD_IO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { LED_RED = 0, LED_BLUE = 1 } led_id_t;
typedef enum { KEY_UP = 0, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_COUNT } key_id_t;

void board_io_init(void);
void led_set(led_id_t led, bool on);
void buzzer_set(bool on);
bool buzzer_is_on(void);
bool key_is_down(key_id_t k);
void key_irq_enable(key_id_t k, bool enable);
const char *key_name(key_id_t k);

/* Hooks called from interrupt context (weak defaults do nothing). */
void app_key_isr(key_id_t k, uint32_t t_us);
void app_imu_drdy_isr(uint32_t t_us);
void ir_rx_edge_isr(uint32_t t_us);   /* PF8 both edges (configured by ir_init) */

void imu_drdy_irq_init(void);   /* PG12 rising edge -> EXTI15_10 */

#endif
