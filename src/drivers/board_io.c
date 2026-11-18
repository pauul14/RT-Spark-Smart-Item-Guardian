/*
 * board_io.c - simple GPIO peripherals of the RT-Spark (schematic sheet 6).
 * Polarity: LEDs active LOW, buzzer active HIGH (NPN driver), keys active LOW.
 */
#include "board_io.h"
#include "board_pins.h"
#include "bsp.h"

typedef struct { GPIO_TypeDef *port; uint16_t pin; IRQn_Type irq; const char *name; } key_hw_t;

static const key_hw_t s_keys[KEY_COUNT] = {
    [KEY_UP]    = { KEY_UP_PORT,    KEY_UP_PIN,    EXTI9_5_IRQn, "UP" },
    [KEY_DOWN]  = { KEY_DOWN_PORT,  KEY_DOWN_PIN,  EXTI1_IRQn,   "DOWN" },
    [KEY_LEFT]  = { KEY_LEFT_PORT,  KEY_LEFT_PIN,  EXTI0_IRQn,   "LEFT" },
    [KEY_RIGHT] = { KEY_RIGHT_PORT, KEY_RIGHT_PIN, EXTI4_IRQn,   "RIGHT" },
};
static volatile bool s_buzzer_on;

void board_io_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* Outputs first written to their inactive level, then configured. */
    HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);

    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = LED_R_PIN | LED_B_PIN;
    HAL_GPIO_Init(GPIOF, &g);
    g.Pin = BUZZER_PIN;
    g.Pull = GPIO_PULLDOWN;                 /* R22 10K already pulls the base low */
    HAL_GPIO_Init(BUZZER_PORT, &g);
    g.Pin = RS485_DE_PIN;                   /* SP3485 RE/DE: receive, never drive the bus */
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(RS485_DE_PORT, &g);

    /* Keys: input pull-up, falling-edge EXTI (press). */
    for (int k = 0; k < KEY_COUNT; k++) {
        g.Pin = s_keys[k].pin;
        g.Mode = GPIO_MODE_IT_FALLING;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(s_keys[k].port, &g);
        HAL_NVIC_SetPriority(s_keys[k].irq, IRQ_PRIO_KEYS, 0);
        HAL_NVIC_EnableIRQ(s_keys[k].irq);
    }
}

void led_set(led_id_t led, bool on)
{
    if (led == LED_RED) HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    else                HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void buzzer_set(bool on)
{
    s_buzzer_on = on;
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool buzzer_is_on(void) { return s_buzzer_on; }

bool key_is_down(key_id_t k)
{
    return HAL_GPIO_ReadPin(s_keys[k].port, s_keys[k].pin) == GPIO_PIN_RESET;
}

void key_irq_enable(key_id_t k, bool enable)
{
    uint32_t line = s_keys[k].pin;          /* EXTI line mask == pin mask */
    if (enable) {
        EXTI->PR = line;                    /* drop edges seen while disabled */
        EXTI->IMR |= line;
    } else {
        EXTI->IMR &= ~line;
    }
}

const char *key_name(key_id_t k) { return k < KEY_COUNT ? s_keys[k].name : "?"; }

void imu_drdy_irq_init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = ICM_INT_PIN;
    g.Mode = GPIO_MODE_IT_RISING;           /* ICM-20608 INT: active high, push-pull, 50 us pulse */
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(ICM_INT_PORT, &g);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, IRQ_PRIO_IMU_DRDY, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ---- weak hooks --------------------------------------------------------- */
__attribute__((weak)) void app_key_isr(key_id_t k, uint32_t t_us) { (void)k; (void)t_us; }
__attribute__((weak)) void app_imu_drdy_isr(uint32_t t_us) { (void)t_us; }
__attribute__((weak)) void ir_rx_edge_isr(uint32_t t_us) { (void)t_us; }

/* ---- EXTI vectors -------------------------------------------------------- */
static inline bool exti_take(uint32_t line)
{
    if (EXTI->PR & line) { EXTI->PR = line; return true; }
    return false;
}

void EXTI0_IRQHandler(void)  { if (exti_take(KEY_LEFT_PIN))  app_key_isr(KEY_LEFT,  sig_time_us()); }
void EXTI1_IRQHandler(void)  { if (exti_take(KEY_DOWN_PIN))  app_key_isr(KEY_DOWN,  sig_time_us()); }
void EXTI4_IRQHandler(void)  { if (exti_take(KEY_RIGHT_PIN)) app_key_isr(KEY_RIGHT, sig_time_us()); }

void EXTI9_5_IRQHandler(void)
{
    uint32_t t = sig_time_us();
    if ((EXTI->IMR & KEY_UP_PIN) && exti_take(KEY_UP_PIN)) app_key_isr(KEY_UP, t);
    if ((EXTI->IMR & IR_RX_PIN) && exti_take(IR_RX_PIN)) ir_rx_edge_isr(t);
    EXTI->PR = EXTI->PR & 0x03E0u & ~EXTI->IMR;   /* clear masked stragglers on lines 5..9 */
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t t = sig_time_us();
    if (exti_take(ICM_INT_PIN)) app_imu_drdy_isr(t);
    EXTI->PR = EXTI->PR & 0xFC00u & ~ICM_INT_PIN;  /* no other line 10..15 is used */
}
