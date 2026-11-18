/*
 * led_matrix.h - 19 x SK6805 (WS2812-compatible) chain on PA7, TIM3_CH2 + DMA1 Stream5.
 * Rows on the PCB (schematic sheet 6): LED1..5, LED6..10, LED11..15, LED16..19.
 */
#ifndef SIG_LED_MATRIX_H
#define SIG_LED_MATRIX_H

#include <stdbool.h>
#include <stdint.h>

bool matrix_init(void);
void matrix_set(int idx, uint8_t r, uint8_t g, uint8_t b);
void matrix_fill(uint8_t r, uint8_t g, uint8_t b);
bool matrix_show(void);               /* false if the previous frame is still being sent */
void matrix_enable(bool on);          /* drives U6 ~OE (PF2): on = buffer enabled */
void matrix_set_brightness(uint8_t max_level);   /* global cap, default 24/255 */
uint32_t matrix_frames_sent(void);

#endif
