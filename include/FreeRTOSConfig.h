/*
 * FreeRTOSConfig.h - Smart Item Guardian (RT-Spark, STM32F407ZGT6 @ 168 MHz)
 *
 * Integration decisions (see docs/firmware_architecture.md):
 *  - SysTick belongs to FreeRTOS (1 kHz). The HAL timebase is moved to TIM6
 *    (src/bsp/hal_timebase_tim6.c) so the two never share a timer.
 *  - SVC/PendSV/SysTick vectors map directly onto the port handlers below;
 *    src/bsp/stm32f4xx_it.c does NOT define those three handlers.
 *  - NVIC priority grouping 4 (4 preemption bits, no sub-priority), required
 *    by the ARM_CM4F port. ISRs that call FreeRTOS "FromISR" APIs use a
 *    numeric priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5).
 *  - The heap (heap_4) is application-allocated in CCM RAM (64 KB, no DMA).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__GNUC__) && !defined(__ASSEMBLER__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
void sig_rtos_assert_failed(const char *file, int line);
void sig_runtime_counter_init(void);
uint32_t sig_runtime_counter_get(void);
#endif

/* ---- Scheduler ---------------------------------------------------------- */
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       (SystemCoreClock)
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     7
#define configMINIMAL_STACK_SIZE                 ((uint16_t)160)
#define configMAX_TASK_NAME_LEN                  12
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TIME_SLICING                   1
#define configUSE_NEWLIB_REENTRANT               0   /* newlib stdio/malloc is not used */
#define configENABLE_BACKWARD_COMPATIBILITY      0

/* ---- Synchronisation primitives ---------------------------------------- */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1
#define configQUEUE_REGISTRY_SIZE                10
#define configUSE_QUEUE_SETS                     0

/* ---- Memory ------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    ((size_t)(48 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP         1   /* ucHeap[] in .ccmram, src/app/rtos_hooks.c */

/* ---- Hooks and diagnostics --------------------------------------------- */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_TRACE_FACILITY                 1   /* uxTaskGetSystemState() for task table */
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configGENERATE_RUN_TIME_STATS            1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() sig_runtime_counter_init()
#define portGET_RUN_TIME_COUNTER_VALUE()         sig_runtime_counter_get()   /* TIM2, 1 MHz, 32 bit */
#define configRECORD_STACK_HIGH_ADDRESS          0

/* ---- Co-routines (unused) ---------------------------------------------- */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          2

/* ---- Software timers (unused: all periodic work is in the five tasks; disabling
 *      them removes the timer service task and its 1 KB stack) ---------------- */
#define configUSE_TIMERS                         0
#define configTIMER_TASK_PRIORITY                3
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256

/* ---- API inclusion ------------------------------------------------------ */
#define INCLUDE_vTaskPrioritySet                 0
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      0
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetIdleTaskHandle           1
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTimerPendFunctionCall           0

/* ---- Cortex-M4 interrupt priorities ------------------------------------ */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                          __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                          4
#endif
/* Lowest priority (15) for PendSV/SysTick. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15
/* ISRs at numeric priority 5..15 may call ...FromISR(); 0..4 must not. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---- Assert ------------------------------------------------------------- */
#define configASSERT(x) do { if ((x) == 0) { sig_rtos_assert_failed(__FILE__, __LINE__); } } while (0)

/* ---- Port handler mapping (CMSIS vector names) --------------------------- */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
