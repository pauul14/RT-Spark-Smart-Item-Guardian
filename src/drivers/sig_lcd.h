/*
 * lcd.h - ST7789V3 240x240 panel on FSMC Bank1/NE3 (8-bit 8080) + text rendering.
 * Owned by the UI task only (no locking inside).
 */
#ifndef SIG_LCD_H
#define SIG_LCD_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_W 240
#define LCD_H 240

/* RGB565 colours */
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define C_RED     0xF800u
#define C_GREEN   0x07E0u
#define C_BLUE    0x001Fu
#define C_CYAN    0x07FFu
#define C_YELLOW  0xFFE0u
#define C_ORANGE  0xFC00u
#define C_GREY    0x8410u
#define C_DKGREY  0x31A6u
#define C_NAVY    0x000Fu

typedef struct {
    bool     present;      /* ID read matched ST7789V3 (0x81B3) */
    uint16_t id;
} lcd_info_t;

bool lcd_init(lcd_info_t *info);
void lcd_backlight(bool on);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_clear(uint16_t color);
/* Draw text with the 5x7 font scaled by `scale`; each cell is 6*scale x 8*scale.
 * Returns the x position after the text. Characters outside the panel are clipped. */
int  lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);
/* Draw text padded with background to exactly `cols` cells (avoids leftovers). */
void lcd_text_field(int x, int y, const char *s, int cols, int scale, uint16_t fg, uint16_t bg);
uint32_t lcd_bus_writes(void);   /* diagnostic counter */

#endif
