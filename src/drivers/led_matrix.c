/*
 * led_matrix.c - SK6805 driver.
 * Bit timing reuses the values proven on this board by the vendor driver
 * (bsp/stm32/stm32f407-rt-spark/board/ports/led_matrix/drv_matrix_led.c,
 * Apache-2.0): TIM3 clock 84 MHz / (PSC 9 + 1) = 8.4 MHz, period 10 ticks
 * (1.19 us), '0' = 2 ticks high (0.24 us), '1' = 7 ticks high (0.83 us), GRB order.
 * Implementation is new: the DMA buffer lives in SRAM (.bss; CCM is not
 * DMA-reachable), and a tail of zero-duty slots provides the >= 80 us reset.
 */
#include "led_matrix.h"
#include "board_pins.h"
#include "bsp.h"
#include <string.h>

#define BITS_PER_LED   24
#define RESET_SLOTS    80                 /* 80 x 1.19 us = 95 us low */
#define BIT0_TICKS     2
#define BIT1_TICKS     7

static TIM_HandleTypeDef s_tim3;
static DMA_HandleTypeDef s_dma;
static uint16_t s_buf[MATRIX_LED_COUNT * BITS_PER_LED + RESET_SLOTS];   /* SRAM, DMA source */
static uint8_t s_rgb[MATRIX_LED_COUNT][3];
static uint8_t s_cap = 24;
static volatile bool s_busy;
static volatile uint32_t s_frames;

void matrix_enable(bool on)
{
    HAL_GPIO_WritePin(MATRIX_EN_PORT, MATRIX_EN_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool matrix_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = MATRIX_EN_PIN;                      /* keep the buffer disabled until configured */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(MATRIX_EN_PORT, MATRIX_EN_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(MATRIX_EN_PORT, &g);

    g.Pin = MATRIX_DATA_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(MATRIX_DATA_PORT, &g);

    s_tim3.Instance = TIM3;
    s_tim3.Init.Prescaler = 9;
    s_tim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim3.Init.Period = 9;
    s_tim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_tim3) != HAL_OK) return false;
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_tim3, &oc, TIM_CHANNEL_2) != HAL_OK) return false;

    s_dma.Instance = DMA1_Stream5;              /* RM0090 table 42: TIM3_CH2 = DMA1 S5 C5 */
    s_dma.Init.Channel = DMA_CHANNEL_5;
    s_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    s_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_dma.Init.MemInc = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    s_dma.Init.Mode = DMA_NORMAL;
    s_dma.Init.Priority = DMA_PRIORITY_HIGH;
    s_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&s_dma) != HAL_OK) return false;
    __HAL_LINKDMA(&s_tim3, hdma[TIM_DMA_ID_CC2], s_dma);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, IRQ_PRIO_DMA_MATRIX, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

    memset(s_rgb, 0, sizeof s_rgb);
    matrix_enable(true);
    return matrix_show();
}

void matrix_set_brightness(uint8_t max_level) { s_cap = max_level; }

void matrix_set(int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i < 0 || i >= MATRIX_LED_COUNT) return;
    s_rgb[i][0] = (uint8_t)((uint16_t)r * s_cap / 255u);
    s_rgb[i][1] = (uint8_t)((uint16_t)g * s_cap / 255u);
    s_rgb[i][2] = (uint8_t)((uint16_t)b * s_cap / 255u);
}

void matrix_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < MATRIX_LED_COUNT; i++) matrix_set(i, r, g, b);
}

bool matrix_show(void)
{
    if (s_busy) return false;
    uint16_t *p = s_buf;
    for (int i = 0; i < MATRIX_LED_COUNT; i++) {
        const uint8_t grb[3] = { s_rgb[i][1], s_rgb[i][0], s_rgb[i][2] };
        for (int c = 0; c < 3; c++)
            for (int bit = 7; bit >= 0; bit--) *p++ = ((grb[c] >> bit) & 1u) ? BIT1_TICKS : BIT0_TICKS;
    }
    for (int i = 0; i < RESET_SLOTS; i++) *p++ = 0;
    s_busy = true;
    if (HAL_TIM_PWM_Start_DMA(&s_tim3, TIM_CHANNEL_2, (uint32_t *)s_buf,
                              (uint16_t)(sizeof s_buf / sizeof s_buf[0])) != HAL_OK) {
        s_busy = false;
        return false;
    }
    return true;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        HAL_TIM_PWM_Stop_DMA(&s_tim3, TIM_CHANNEL_2);
        __HAL_TIM_SET_COMPARE(&s_tim3, TIM_CHANNEL_2, 0);
        s_busy = false;
        s_frames++;
    }
}

uint32_t matrix_frames_sent(void) { return s_frames; }

void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&s_dma); }
