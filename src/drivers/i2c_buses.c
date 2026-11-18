#include "i2c_buses.h"
#include "board_pins.h"
#include "bsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

volatile uint32_t g_i2c2_errors;
volatile uint32_t g_i2c2_recoveries;
volatile uint32_t g_swi2c_errors;

static I2C_HandleTypeDef s_i2c2;
static SemaphoreHandle_t s_i2c2_mutex;

#define I2C2_TIMEOUT_MS   5u
#define I2C2_LOCK_MS      20u

static void i2c2_pins_af(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = I2C2_SCL_PIN | I2C2_SDA_PIN;
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;                    /* external 5.1K pull-ups (R1/R2) */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOF, &g);
}

bool i2c2_init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    i2c2_pins_af();
    s_i2c2.Instance = I2C2;
    s_i2c2.Init.ClockSpeed = 400000;
    s_i2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_i2c2.Init.OwnAddress1 = 0;
    s_i2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&s_i2c2) == HAL_OK;
}

void i2c2_rtos_init(void)
{
    if (!s_i2c2_mutex) {
        s_i2c2_mutex = xSemaphoreCreateMutex();
        vQueueAddToRegistry(s_i2c2_mutex, "i2c2");
    }
}

static bool lock(void)
{
    if (!s_i2c2_mutex || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return true;
    return xSemaphoreTake(s_i2c2_mutex, pdMS_TO_TICKS(I2C2_LOCK_MS)) == pdTRUE;
}

static void unlock(void)
{
    if (s_i2c2_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGive(s_i2c2_mutex);
}

/* Bus recovery (I2C-bus spec UM10204 3.1.16): clock SCL until a stuck slave
 * releases SDA, then issue STOP and re-initialise the peripheral. Caller holds the lock. */
static void recover_locked(void)
{
    HAL_I2C_DeInit(&s_i2c2);
    GPIO_InitTypeDef g = {0};
    g.Pin = I2C2_SCL_PIN | I2C2_SDA_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_WritePin(GPIOF, I2C2_SCL_PIN | I2C2_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOF, &g);
    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOF, I2C2_SDA_PIN) == GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(GPIOF, I2C2_SCL_PIN, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 200; d++) { }
        HAL_GPIO_WritePin(GPIOF, I2C2_SCL_PIN, GPIO_PIN_SET);
        for (volatile int d = 0; d < 200; d++) { }
    }
    HAL_GPIO_WritePin(GPIOF, I2C2_SDA_PIN, GPIO_PIN_RESET);   /* STOP: SDA low->high while SCL high */
    for (volatile int d = 0; d < 200; d++) { }
    HAL_GPIO_WritePin(GPIOF, I2C2_SDA_PIN, GPIO_PIN_SET);
    I2C2->CR1 |= I2C_CR1_SWRST;
    I2C2->CR1 &= ~I2C_CR1_SWRST;
    i2c2_init();
    g_i2c2_recoveries++;
}

void i2c2_recover(void)
{
    if (!lock()) return;
    recover_locked();
    unlock();
}

static int finish(HAL_StatusTypeDef st)
{
    if (st == HAL_OK) return I2C_OK;
    g_i2c2_errors++;
    uint32_t err = HAL_I2C_GetError(&s_i2c2);
    if (st == HAL_TIMEOUT || st == HAL_BUSY || (err & (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_TIMEOUT))) {
        recover_locked();
        return I2C_ERR_BUS;
    }
    return (err & HAL_I2C_ERROR_AF) ? I2C_ERR_NACK : I2C_ERR_BUS;
}

int i2c2_mem_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t n)
{
    if (!lock()) return I2C_ERR_LOCK;
    int r = finish(HAL_I2C_Mem_Read(&s_i2c2, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, n, I2C2_TIMEOUT_MS));
    unlock();
    return r;
}

int i2c2_mem_write(uint8_t addr7, uint8_t reg, const uint8_t *buf, uint16_t n)
{
    if (!lock()) return I2C_ERR_LOCK;
    int r = finish(HAL_I2C_Mem_Write(&s_i2c2, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT,
                                     (uint8_t *)buf, n, I2C2_TIMEOUT_MS));
    unlock();
    return r;
}

int i2c2_write_reg(uint8_t addr7, uint8_t reg, uint8_t val) { return i2c2_mem_write(addr7, reg, &val, 1); }

bool i2c2_probe(uint8_t addr7)
{
    if (!lock()) return false;
    bool ok = HAL_I2C_IsDeviceReady(&s_i2c2, (uint16_t)(addr7 << 1), 2, I2C2_TIMEOUT_MS) == HAL_OK;
    unlock();
    return ok;
}

/* ===================== software I2C (PE0 = SCL, PE1 = SDA) ================= */
#define SW_HALF_US 5u      /* ~100 kHz */

static inline void sw_delay(void)
{
    uint32_t start = DWT->CYCCNT, cyc = SW_HALF_US * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < cyc) { }
}
static inline void scl(bool h) { HAL_GPIO_WritePin(AHT_SCL_PORT, AHT_SCL_PIN, h ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static inline void sda(bool h) { HAL_GPIO_WritePin(AHT_SDA_PORT, AHT_SDA_PIN, h ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static inline bool sda_in(void) { return HAL_GPIO_ReadPin(AHT_SDA_PORT, AHT_SDA_PIN) == GPIO_PIN_SET; }

static bool scl_release(void)
{
    scl(true);
    for (int i = 0; i < 1000; i++) {               /* allow clock stretching (<= ~5 ms) */
        if (HAL_GPIO_ReadPin(AHT_SCL_PORT, AHT_SCL_PIN) == GPIO_PIN_SET) return true;
        sw_delay();
    }
    return false;
}

void sw_i2c_init(void)
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = AHT_SCL_PIN | AHT_SDA_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;                 /* external 5.1K pull-ups (R6/R7) */
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOE, AHT_SCL_PIN | AHT_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOE, &g);
}

static void sw_start(void) { sda(true); scl_release(); sw_delay(); sda(false); sw_delay(); scl(false); sw_delay(); }
static void sw_stop(void)  { sda(false); sw_delay(); scl_release(); sw_delay(); sda(true); sw_delay(); }

static bool sw_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        sda((b >> i) & 1u);
        sw_delay();
        if (!scl_release()) return false;
        sw_delay();
        scl(false);
    }
    sda(true);                                   /* release for ACK */
    sw_delay();
    if (!scl_release()) return false;
    bool ack = !sda_in();
    sw_delay();
    scl(false);
    return ack;
}

static uint8_t sw_read_byte(bool ack)
{
    uint8_t b = 0;
    sda(true);
    for (int i = 0; i < 8; i++) {
        sw_delay();
        scl_release();
        b = (uint8_t)((b << 1) | (sda_in() ? 1u : 0u));
        sw_delay();
        scl(false);
    }
    sda(!ack);
    sw_delay();
    scl_release();
    sw_delay();
    scl(false);
    sda(true);
    return b;
}

int sw_i2c_write(uint8_t addr7, const uint8_t *data, uint16_t n)
{
    sw_start();
    if (!sw_write_byte((uint8_t)(addr7 << 1))) { sw_stop(); g_swi2c_errors++; return I2C_ERR_NACK; }
    for (uint16_t i = 0; i < n; i++) {
        if (!sw_write_byte(data[i])) { sw_stop(); g_swi2c_errors++; return I2C_ERR_NACK; }
    }
    sw_stop();
    return I2C_OK;
}

int sw_i2c_read(uint8_t addr7, uint8_t *data, uint16_t n)
{
    sw_start();
    if (!sw_write_byte((uint8_t)((addr7 << 1) | 1u))) { sw_stop(); g_swi2c_errors++; return I2C_ERR_NACK; }
    for (uint16_t i = 0; i < n; i++) data[i] = sw_read_byte(i + 1 < n);
    sw_stop();
    return I2C_OK;
}
