/*
 * sd_card.h - microSD over SDIO (schematic sheet 7: D0..D3 PC8..PC11, CK PC12,
 * CMD PD2, card detect PF3) and the FatFs disk-I/O layer.
 *
 * Transfers use HAL polling in 4-bit mode at 8 MHz. Hardware flow control is NOT
 * used (STM32F40x errata ES0182: SDIO_CK glitches with HW flow control), so the
 * FIFO data phase of each block runs inside a short FreeRTOS critical section
 * (about 0.2 ms per 512 B block) to rule out FIFO under/overrun; the card's
 * programming-busy wait happens outside it. Only the health task uses the card.
 */
#ifndef SIG_SD_CARD_H
#define SIG_SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     present;        /* initialised and answering */
    bool     wide_bus;       /* 4-bit mode active */
    uint32_t card_type;      /* HAL CARD_SDSC / CARD_SDHC_SDXC */
    uint32_t blocks;         /* 512-byte logical blocks */
    int      cd_level;       /* raw PF3 level (polarity to be confirmed on hardware) */
    uint32_t init_error;     /* HAL error code of the last failed init */
} sd_info_t;

bool sd_card_init(sd_info_t *info);          /* (re)initialise, 4-bit, 8 MHz */
void sd_card_deinit(void);
int  sd_card_detect_level(void);
bool sd_card_read(uint32_t block, uint8_t *buf, uint32_t count);
bool sd_card_write(uint32_t block, const uint8_t *buf, uint32_t count);
const sd_info_t *sd_card_info(void);

extern volatile uint32_t g_sd_read_err, g_sd_write_err;

#endif
