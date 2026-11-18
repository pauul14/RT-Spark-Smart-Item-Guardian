/*
 * bsp.h - board bring-up services: clocks, time bases, reset/fault records.
 */
#ifndef SIG_BSP_H
#define SIG_BSP_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- Interrupt priorities (NVIC group 4: 0 = highest, 15 = lowest) ------- *
 * 0..4  : may NOT call FreeRTOS APIs (above configMAX_SYSCALL_INTERRUPT_PRIORITY)
 * 5..15 : may call ...FromISR() APIs                                          */
#define IRQ_PRIO_HAL_TICK      3   /* TIM6: HAL_IncTick only (TICK_INT_PRIORITY) */
#define IRQ_PRIO_IMU_DRDY      5   /* EXTI15_10: ICM-20608 data ready -> task notify */
#define IRQ_PRIO_DMA_MATRIX    6   /* DMA1_Stream5: LED matrix frame complete */
#define IRQ_PRIO_UART          6   /* USART1 console RX/TX */
#define IRQ_PRIO_KEYS          7   /* EXTI0/1/4/9_5: keys */
#define IRQ_PRIO_IR_RX         8   /* EXTI9_5 line 8 shares the key vector (priority 7) */

typedef struct {
    bool     hse_ok;            /* false: running from HSI fallback */
    uint32_t sysclk_hz, hclk_hz, pclk1_hz, pclk2_hz;
} sig_clock_info_t;

typedef struct {
    uint32_t csr;               /* RCC->CSR snapshot at boot */
    const char *cause;          /* human readable dominant cause */
} sig_reset_info_t;

/* Fault record kept in .noinit across a soft/watchdog reset. */
#define SIG_FAULT_MAGIC 0x5F1A7E11u
typedef struct {
    uint32_t magic;
    uint32_t type;              /* 1 HardFault 2 MemManage 3 BusFault 4 UsageFault 5 stack overflow 6 malloc 7 assert 8 watchdog-stall */
    uint32_t pc, lr, psr;
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t aux;               /* line number / stalled-task mask */
    char     tag[16];           /* task name or file tail */
    uint32_t boot_count;
} sig_fault_record_t;

extern sig_clock_info_t   g_clock;
extern sig_reset_info_t   g_reset;

void sig_clock_init(void);
void sig_reset_capture(void);

/* Free-running microsecond time (TIM2, 32-bit, wraps every ~71.6 min). */
void     sig_time_init(void);
static inline uint32_t sig_time_us(void) { return TIM2->CNT; }
static inline uint32_t sig_cycles(void) { return DWT->CYCCNT; }
uint32_t sig_time_ms(void);

/* Fault handling */
void sig_fault_record(uint32_t type, const uint32_t *frame, uint32_t aux, const char *tag);
bool sig_fault_take_previous(sig_fault_record_t *out);   /* returns + clears last record */
uint32_t sig_boot_count(void);
const char *sig_fault_type_name(uint32_t type);
void sig_fatal_early(const char *msg);                    /* before the scheduler only */

/* Blocking console output for early boot / fault paths (no RTOS). */
void sig_uart_puts_blocking(const char *s);

/* Delay that works both before and after the scheduler starts. */
void sig_delay_ms(uint32_t ms);

#endif /* SIG_BSP_H */
