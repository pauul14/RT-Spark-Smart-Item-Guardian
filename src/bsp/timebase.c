/*
 * timebase.c
 *  - HAL timebase on TIM6 (overrides the weak HAL_InitTick/Suspend/Resume),
 *    because SysTick is owned by the FreeRTOS port.
 *  - TIM2 as a free-running 32-bit 1 MHz counter: sample timestamps, latency
 *    measurements and the FreeRTOS run-time-stats clock.
 *  - DWT cycle counter for sub-microsecond inference timing.
 */
#include "bsp.h"
#include "FreeRTOS.h"
#include "task.h"

/* APB1 timer clock = PCLK1 * 2 when the APB1 prescaler is not 1 (RM0090 6.2). */
static uint32_t apb1_timer_clock(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2u * pclk1;
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0;
    TIM6->PSC = (uint16_t)(apb1_timer_clock() / 1000000u - 1u);   /* 1 MHz */
    /* uwTickFreq is the tick PERIOD in ms in HAL 1.8.x (HAL_TICK_FREQ_1KHZ == 1). */
    TIM6->ARR = 1000u * (uint32_t)uwTickFreq - 1u;
    TIM6->EGR = TIM_EGR_UG;                                        /* load PSC */
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    uwTickPrio = TickPriority;
    TIM6->CR1 = TIM_CR1_CEN;
    return HAL_OK;
}

void HAL_SuspendTick(void) { TIM6->DIER &= ~TIM_DIER_UIE; }
void HAL_ResumeTick(void)  { TIM6->DIER |= TIM_DIER_UIE; }

void TIM6_DAC_IRQHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}

void sig_time_init(void)
{
    /* TIM2: 32-bit up-counter at 1 MHz, never stopped, no interrupt. */
    __HAL_RCC_TIM2_CLK_ENABLE();
    if (!(TIM2->CR1 & TIM_CR1_CEN)) {
        TIM2->PSC = (uint16_t)(apb1_timer_clock() / 1000000u - 1u);
        TIM2->ARR = 0xFFFFFFFFu;
        TIM2->EGR = TIM_EGR_UG;
        TIM2->CR1 = TIM_CR1_CEN;
    }
    /* DWT CYCCNT */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void sig_runtime_counter_init(void) { sig_time_init(); }
uint32_t sig_runtime_counter_get(void) { return TIM2->CNT; }

uint32_t sig_time_ms(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        return (uint32_t)xTaskGetTickCount();          /* 1 kHz tick */
    }
    return HAL_GetTick();
}

void sig_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}
