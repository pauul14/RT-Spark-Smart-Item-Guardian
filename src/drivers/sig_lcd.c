/*
 * lcd.c - ST7789V3 over FSMC. Wiring: schematic sheet 8 (CN2 FPC_LCD_24P).
 * The FSMC timing values and the panel register sequence (porch, gate, VCOM,
 * power and gamma values) are taken from the vendor driver
 * RT-Thread bsp/stm32/stm32f407-rt-spark/board/ports/lcd/drv_lcd.c (Apache-2.0),
 * which is the tested configuration for this exact panel; the code is new.
 */
#include "sig_lcd.h"
#include "board_pins.h"
#include "bsp.h"
#include <string.h>

extern const uint8_t g_font5x7[95][5];

#define LCD_CMD  (*(volatile uint8_t *)LCD_CMD_ADDR)
#define LCD_DATA (*(volatile uint8_t *)LCD_DATA_ADDR)

static SRAM_HandleTypeDef s_sram;
static uint32_t s_writes;

static inline void wr_cmd(uint8_t c) { LCD_CMD = c; }
static inline void wr_dat(uint8_t d) { LCD_DATA = d; }
static inline uint8_t rd_dat(void) { return LCD_DATA; }

static void fsmc_gpio_init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF12_FSMC;
    /* PD0 D2, PD1 D3, PD4 NOE, PD5 NWE, PD13 A18, PD14 D0, PD15 D1 */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);
    /* PE7..PE10 = D4..D7 */
    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &g);
    /* PG10 = NE3 */
    g.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOG, &g);

    /* Reset and backlight as plain outputs. */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = 0;
    g.Pin = LCD_RST_PIN;
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(LCD_RST_PORT, &g);
    g.Pin = LCD_BL_PIN;
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_Init(LCD_BL_PORT, &g);
}

static bool fsmc_init(void)
{
    FSMC_NORSRAM_TimingTypeDef rd = {0}, wr = {0};
    s_sram.Instance = FSMC_NORSRAM_DEVICE;
    s_sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    s_sram.Init.NSBank = FSMC_NORSRAM_BANK3;
    s_sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    s_sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    s_sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    s_sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    s_sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    s_sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    s_sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    s_sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    s_sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    s_sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    s_sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    s_sram.Init.PageSize = FSMC_PAGE_SIZE_NONE;
    /* Conservative timings for init and ID read (vendor values, mode A). */
    rd.AddressSetupTime = 15; rd.AddressHoldTime = 0; rd.DataSetupTime = 60;
    rd.BusTurnAroundDuration = 0; rd.CLKDivision = 0; rd.DataLatency = 0; rd.AccessMode = FSMC_ACCESS_MODE_A;
    wr.AddressSetupTime = 9; wr.AddressHoldTime = 0; wr.DataSetupTime = 8;
    wr.BusTurnAroundDuration = 0; wr.CLKDivision = 0; wr.DataLatency = 0; wr.AccessMode = FSMC_ACCESS_MODE_A;
    return HAL_SRAM_Init(&s_sram, &rd, &wr) == HAL_OK;
}

static void seq(uint8_t cmd, const uint8_t *d, int n)
{
    wr_cmd(cmd);
    for (int i = 0; i < n; i++) wr_dat(d[i]);
}

bool lcd_init(lcd_info_t *info)
{
    info->present = false;
    info->id = 0;
    fsmc_gpio_init();
    if (!fsmc_init()) return false;

    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    sig_delay_ms(20);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    sig_delay_ms(120);

    /* RDDID (0x04): dummy, ID1, ID2, ID3 -> vendor checks ID2:ID3 == 0x81B3 */
    wr_cmd(0x04);
    (void)rd_dat();
    (void)rd_dat();
    uint16_t id = (uint16_t)rd_dat() << 8;
    id |= rd_dat();
    info->id = id;
    info->present = (id == 0x81B3u);

    static const uint8_t madctl[] = {0x00}, colmod[] = {0x65 /* vendor value: 16 bpp */};
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gate[] = {0x35}, vcom[] = {0x37}, lcm[] = {0x2C}, vdvvrh[] = {0x01};
    static const uint8_t vrh[] = {0x12}, vdv[] = {0x20}, frctl[] = {0x0F}, pwr[] = {0xA4, 0xA1};
    static const uint8_t gp[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    static const uint8_t gn[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    static const uint8_t te[] = {0x00};
    seq(0x36, madctl, 1);
    seq(0x3A, colmod, 1);
    seq(0xB2, porch, 5);
    seq(0xB7, gate, 1);
    seq(0xBB, vcom, 1);
    seq(0xC0, lcm, 1);
    seq(0xC2, vdvvrh, 1);
    seq(0xC3, vrh, 1);
    seq(0xC4, vdv, 1);
    seq(0xC6, frctl, 1);
    seq(0xD0, pwr, 2);
    seq(0xE0, gp, 14);
    seq(0xE1, gn, 14);
    wr_cmd(0x21);                 /* display inversion on (panel requires it) */
    seq(0x35, te, 1);             /* tearing effect line on */
    wr_cmd(0x11);                 /* sleep out */
    sig_delay_ms(120);
    wr_cmd(0x29);                 /* display on */

    /* Note: the vendor driver later writes ADDSET 3 / DATAST 2 into FSMC_Bank1E->BWTR[6],
     * which is bank 4 (HAL: FSMC_NORSRAM_BANK3 == index 4), so its panel actually ran
     * with the write timing above (~18 HCLK = 107 ns per byte). 3/2 would give a
     * ~36 ns cycle, below the ST7789 66 ns minimum write cycle, so the tested timing
     * is kept (full-screen fill ~= 12 ms). */
    lcd_clear(C_BLACK);
    lcd_backlight(true);
    return info->present;
}

void lcd_backlight(bool on)
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void set_window(int x0, int y0, int x1, int y1)
{
    wr_cmd(0x2A);
    wr_dat((uint8_t)(x0 >> 8)); wr_dat((uint8_t)x0); wr_dat((uint8_t)(x1 >> 8)); wr_dat((uint8_t)x1);
    wr_cmd(0x2B);
    wr_dat((uint8_t)(y0 >> 8)); wr_dat((uint8_t)y0); wr_dat((uint8_t)(y1 >> 8)); wr_dat((uint8_t)y1);
    wr_cmd(0x2C);
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;
    set_window(x, y, x + w - 1, y + h - 1);
    uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)c;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    for (uint32_t i = 0; i < n; i++) { wr_dat(hi); wr_dat(lo); }
    s_writes += n;
}

void lcd_clear(uint16_t c) { lcd_fill_rect(0, 0, LCD_W, LCD_H, c); }

static void draw_char(int x, int y, char ch, int s, uint16_t fg, uint16_t bg)
{
    int cw = 6 * s, chh = 8 * s;
    if (x < 0 || y < 0 || x + cw > LCD_W || y + chh > LCD_H) return;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *gl = g_font5x7[ch - 32];
    set_window(x, y, x + cw - 1, y + chh - 1);
    uint8_t fh = (uint8_t)(fg >> 8), fl = (uint8_t)fg, bh = (uint8_t)(bg >> 8), bl = (uint8_t)bg;
    for (int r = 0; r < chh; r++) {
        int gr = r / s;
        for (int c = 0; c < cw; c++) {
            int gc = c / s;
            bool on = gc < 5 && gr < 8 && ((gl[gc] >> gr) & 1u);
            if (on) { wr_dat(fh); wr_dat(fl); } else { wr_dat(bh); wr_dat(bl); }
        }
    }
    s_writes += (uint32_t)(cw * chh);
}

int lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg)
{
    while (*s) {
        draw_char(x, y, *s++, scale, fg, bg);
        x += 6 * scale;
    }
    return x;
}

void lcd_text_field(int x, int y, const char *s, int cols, int scale, uint16_t fg, uint16_t bg)
{
    int i = 0;
    for (; i < cols && s[i]; i++) draw_char(x + i * 6 * scale, y, s[i], scale, fg, bg);
    if (i < cols) lcd_fill_rect(x + i * 6 * scale, y, (cols - i) * 6 * scale, 8 * scale, bg);
}

uint32_t lcd_bus_writes(void) { return s_writes; }
