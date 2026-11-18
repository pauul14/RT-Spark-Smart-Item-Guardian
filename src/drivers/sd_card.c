#include "sd_card.h"
#include "board_pins.h"
#include "bsp.h"
#include "rtc_clock.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>

volatile uint32_t g_sd_read_err, g_sd_write_err;

static SD_HandleTypeDef s_sd;
static sd_info_t s_info;
static uint8_t s_bounce[512] __attribute__((aligned(4)));   /* SRAM bounce buffer for unaligned FatFs buffers */

#define SD_XFER_CLKDIV   4u      /* 48 MHz / (4 + 2) = 8 MHz */
#define SD_BUSY_MS       500u    /* card programming / transfer state timeout */

static void pins(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = SD_CD_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;                         /* external pull-up on SD_CD */
    HAL_GPIO_Init(SD_CD_PORT, &g);
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;                         /* 47K externals present too */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF12_SDIO;
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &g);
}

int sd_card_detect_level(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    return HAL_GPIO_ReadPin(SD_CD_PORT, SD_CD_PIN) == GPIO_PIN_SET ? 1 : 0;
}

const sd_info_t *sd_card_info(void) { return &s_info; }

void sd_card_deinit(void)
{
    if (s_sd.Instance) HAL_SD_DeInit(&s_sd);
    s_info.present = false;
    s_info.wide_bus = false;
}

bool sd_card_init(sd_info_t *info)
{
    pins();
    __HAL_RCC_SDIO_CLK_ENABLE();
    sd_card_deinit();
    memset(&s_info, 0, sizeof s_info);
    s_info.cd_level = sd_card_detect_level();
    s_sd.Instance = SDIO;
    s_sd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    s_sd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    s_sd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    s_sd.Init.BusWide = SDIO_BUS_WIDE_1B;         /* HAL_SD_Init identifies the card at 400 kHz */
    s_sd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;   /* errata ES0182 */
    s_sd.Init.ClockDiv = SD_XFER_CLKDIV;
    if (HAL_SD_Init(&s_sd) != HAL_OK) {
        s_info.init_error = s_sd.ErrorCode;
        HAL_SD_DeInit(&s_sd);
        if (info) *info = s_info;
        return false;
    }
    s_info.wide_bus = HAL_SD_ConfigWideBusOperation(&s_sd, SDIO_BUS_WIDE_4B) == HAL_OK;
    HAL_SD_CardInfoTypeDef ci;
    HAL_SD_GetCardInfo(&s_sd, &ci);
    s_info.card_type = ci.CardType;
    s_info.blocks = ci.LogBlockNbr;
    s_info.present = true;
    if (info) *info = s_info;
    return true;
}

static bool wait_transfer_state(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&s_sd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > SD_BUSY_MS) return false;
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(1);
    }
    return true;
}

/* One block at a time; the FIFO phase is protected from preemption. */
static bool xfer_block(uint32_t block, uint8_t *buf, bool write)
{
    HAL_StatusTypeDef st;
    bool rtos = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (rtos) taskENTER_CRITICAL();
    st = write ? HAL_SD_WriteBlocks(&s_sd, buf, block, 1, 100) : HAL_SD_ReadBlocks(&s_sd, buf, block, 1, 100);
    if (rtos) taskEXIT_CRITICAL();
    return st == HAL_OK && wait_transfer_state();
}

bool sd_card_read(uint32_t block, uint8_t *buf, uint32_t count)
{
    if (!s_info.present) return false;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *dst = buf + i * 512u;
        bool aligned = ((uintptr_t)dst & 3u) == 0;
        if (!xfer_block(block + i, aligned ? dst : s_bounce, false)) { g_sd_read_err++; return false; }
        if (!aligned) memcpy(dst, s_bounce, 512);
    }
    return true;
}

bool sd_card_write(uint32_t block, const uint8_t *buf, uint32_t count)
{
    if (!s_info.present) return false;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(s_bounce, buf + i * 512u, 512);
        if (!xfer_block(block + i, s_bounce, true)) { g_sd_write_err++; return false; }
    }
    return true;
}

/* ===================== FatFs disk I/O (single drive 0) ===================== */
DSTATUS disk_status(BYTE pdrv) { return (pdrv == 0 && s_info.present) ? 0 : STA_NOINIT; }

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (!s_info.present && !sd_card_init(NULL)) return STA_NOINIT;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !s_info.present) return RES_NOTRDY;
    return sd_card_read(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !s_info.present) return RES_NOTRDY;
    return sd_card_write(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || !s_info.present) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: return wait_transfer_state() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(DWORD *)buff = s_info.blocks; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

/* FAT timestamp from the RTC (falls back to the _NORTC date if the RTC was never set). */
DWORD get_fattime(void)
{
    rtc_time_t t;
    if (!rtc_time_valid() || !rtc_get(&t)) {
        return ((DWORD)(_NORTC_YEAR - 1980) << 25) | ((DWORD)_NORTC_MON << 21) | ((DWORD)_NORTC_MDAY << 16);
    }
    return ((DWORD)(t.year - 1980) << 25) | ((DWORD)t.mon << 21) | ((DWORD)t.day << 16) |
           ((DWORD)t.hour << 11) | ((DWORD)t.min << 5) | ((DWORD)(t.sec / 2));
}
