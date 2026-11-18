#include "spi_flash.h"
#include "board_pins.h"
#include "bsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SPI_HandleTypeDef s_spi2;
static SemaphoreHandle_t s_spi2_mutex;

#define CS_LOW()  HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET)
#define CS_HIGH() HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET)

bool spi2_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    /* Both chip selects high before anything else. */
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    g.Pin = FLASH_CS_PIN;
    HAL_GPIO_Init(FLASH_CS_PORT, &g);
    g.Pin = WIFI_CS_PIN;
    HAL_GPIO_Init(WIFI_CS_PORT, &g);

    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI2;
    g.Pin = SPI2_SCK_PIN;
    HAL_GPIO_Init(SPI2_SCK_PORT, &g);
    g.Pin = SPI2_MISO_PIN | SPI2_MOSI_PIN;
    HAL_GPIO_Init(GPIOC, &g);

    s_spi2.Instance = SPI2;
    s_spi2.Init.Mode = SPI_MODE_MASTER;
    s_spi2.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi2.Init.DataSize = SPI_DATASIZE_8BIT;
    s_spi2.Init.CLKPolarity = SPI_POLARITY_LOW;       /* mode 0 */
    s_spi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_spi2.Init.NSS = SPI_NSS_SOFT;
    s_spi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;   /* 42 MHz / 8 = 5.25 MHz */
    s_spi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi2.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_spi2.Init.CRCPolynomial = 7;
    return HAL_SPI_Init(&s_spi2) == HAL_OK;
}

void spi2_rtos_init(void)
{
    if (!s_spi2_mutex) {
        s_spi2_mutex = xSemaphoreCreateMutex();
        vQueueAddToRegistry(s_spi2_mutex, "spi2");
    }
}

bool spi2_lock(uint32_t timeout_ms)
{
    if (!s_spi2_mutex || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return true;
    return xSemaphoreTake(s_spi2_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void spi2_unlock(void)
{
    if (s_spi2_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGive(s_spi2_mutex);
}

static bool xfer(const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    if (rx && tx) return HAL_SPI_TransmitReceive(&s_spi2, (uint8_t *)tx, rx, n, 20) == HAL_OK;
    if (tx) return HAL_SPI_Transmit(&s_spi2, (uint8_t *)tx, n, 20) == HAL_OK;
    return HAL_SPI_Receive(&s_spi2, rx, n, 50) == HAL_OK;
}

static bool cmd_only(uint8_t c)
{
    CS_LOW();
    bool ok = xfer(&c, NULL, 1);
    CS_HIGH();
    return ok;
}

bool w25q_read_status(uint8_t *sr1)
{
    uint8_t tx[2] = {0x05, 0xFF}, rx[2] = {0};
    CS_LOW();
    bool ok = xfer(tx, rx, 2);
    CS_HIGH();
    *sr1 = rx[1];
    return ok;
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t sr = 0;
    do {
        if (!w25q_read_status(&sr)) return false;
        if (!(sr & 0x01u)) return true;           /* BUSY clear */
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(1);
    } while ((HAL_GetTick() - t0) < timeout_ms);
    return false;
}

bool w25q_read_id(w25q_info_t *info)
{
    uint8_t tx[4] = {0x9F, 0xFF, 0xFF, 0xFF}, rx[4] = {0};
    CS_LOW();
    bool ok = xfer(tx, rx, 4);
    CS_HIGH();
    info->manufacturer = rx[1];
    info->mem_type = rx[2];
    info->capacity = rx[3];
    info->present = ok && rx[1] == 0xEF && rx[3] >= 0x10 && rx[3] <= 0x20;
    info->size_bytes = info->present ? (1u << rx[3]) : 0;
    return info->present;
}

bool w25q_read(uint32_t addr, uint8_t *buf, uint32_t n)
{
    uint8_t h[4] = {0x03, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    CS_LOW();
    bool ok = xfer(h, NULL, 4);
    while (ok && n) {
        uint16_t chunk = n > 4096 ? 4096 : (uint16_t)n;
        ok = xfer(NULL, buf, chunk);
        buf += chunk;
        n -= chunk;
    }
    CS_HIGH();
    return ok;
}

bool w25q_program_page(uint32_t addr, const uint8_t *buf, uint32_t n)
{
    if (n == 0 || n > W25Q_PAGE || ((addr % W25Q_PAGE) + n) > W25Q_PAGE) return false;
    if (!cmd_only(0x06)) return false;            /* WRITE ENABLE */
    uint8_t h[4] = {0x02, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    CS_LOW();
    bool ok = xfer(h, NULL, 4) && xfer(buf, NULL, (uint16_t)n);
    CS_HIGH();
    return ok && wait_ready(10);
}

bool w25q_erase_sector(uint32_t addr)
{
    if (!cmd_only(0x06)) return false;
    uint8_t h[4] = {0x20, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    CS_LOW();
    bool ok = xfer(h, NULL, 4);
    CS_HIGH();
    return ok && wait_ready(500);                 /* tSE max 400 ms */
}
