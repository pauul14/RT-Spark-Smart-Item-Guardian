/*
 * spi_flash.h - SPI2 bus (shared by W25Q128 /CE=PB12 and RW007 nCS=PF10) and
 * the W25Q128JV driver. The RW007 chip select is forced HIGH at init so the
 * Wi-Fi module never drives MISO while the flash is used.
 */
#ifndef SIG_SPI_FLASH_H
#define SIG_SPI_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#define W25Q_PAGE      256u
#define W25Q_SECTOR    4096u

typedef struct {
    uint8_t  manufacturer;   /* 0xEF Winbond */
    uint8_t  mem_type;       /* 0x40 */
    uint8_t  capacity;       /* 0x18 -> 16 MB */
    uint32_t size_bytes;
    bool     present;
} w25q_info_t;

bool spi2_init(void);
void spi2_rtos_init(void);
bool spi2_lock(uint32_t timeout_ms);
void spi2_unlock(void);

/* All w25q_* calls expect the caller to hold spi2_lock(). */
bool w25q_read_id(w25q_info_t *info);
bool w25q_read(uint32_t addr, uint8_t *buf, uint32_t n);
bool w25q_program_page(uint32_t addr, const uint8_t *buf, uint32_t n);   /* n <= 256, no page crossing */
bool w25q_erase_sector(uint32_t addr);                                   /* 4 KB */
bool w25q_read_status(uint8_t *sr1);

#endif
