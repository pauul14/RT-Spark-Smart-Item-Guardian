/*
 * Smart Item Guardian - BCA182 Laboratory Activity 4
 * RT-Thread RT-Spark (STM32F407ZGT6), STM32Cube HAL + FreeRTOS 10.3.1.
 *
 * Boot order:
 *  1. HAL_Init (flash prefetch/caches, NVIC group 4, HAL tick on TIM6)
 *  2. reset cause, 168 MHz clock tree (HSE, HSI fallback), TIM2/DWT time bases
 *  3. console UART, RTOS objects (queues, mutexes, stream buffers)
 *  4. peripheral bring-up (LCD, LED matrix, I2C2 sensors, AHT21, SPI flash, RTC)
 *  5. task creation, scheduler start (SysTick/PendSV/SVC owned by FreeRTOS)
 */
#include "stm32f4xx_hal.h"
#include "bsp.h"
#include "app.h"
#include "uart_console.h"
#include "mini_printf.h"

void HAL_MspInit(void)
{
    /* All 4 bits pre-emption priority, as required by the FreeRTOS ARM_CM4F port. */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

int main(void)
{
    HAL_Init();
    sig_reset_capture();
    sig_clock_init();
    sig_time_init();
    /* Freeze IWDG and the time bases while the core is halted by a debugger. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP | DBGMCU_APB1_FZ_DBG_TIM2_STOP | DBGMCU_APB1_FZ_DBG_TIM6_STOP;

    uart_console_init(CONSOLE_BAUD);
    sig_uart_puts_blocking("\r\n#BOOT," FW_NAME "," FW_VERSION "\r\n");

    app_create_rtos_objects();
    sig_log("#BOOT,fw=%s,version=%s,built=%s %s,boot_count=%lu,reset=%s,csr=0x%08lx",
            FW_NAME, FW_VERSION, __DATE__, __TIME__, (unsigned long)sig_boot_count(), g_reset.cause,
            (unsigned long)g_reset.csr);
    sig_log("#CLOCK,hse_ok=%d,sysclk=%lu,hclk=%lu,pclk1=%lu,pclk2=%lu", g_clock.hse_ok,
            (unsigned long)g_clock.sysclk_hz, (unsigned long)g_clock.hclk_hz,
            (unsigned long)g_clock.pclk1_hz, (unsigned long)g_clock.pclk2_hz);
    sig_fault_record_t fr;
    if (sig_fault_take_previous(&fr)) {
        sig_log("#FAULT,previous,type=%s,pc=0x%08lx,lr=0x%08lx,cfsr=0x%08lx,hfsr=0x%08lx,aux=%lu,tag=%s",
                sig_fault_type_name(fr.type), (unsigned long)fr.pc, (unsigned long)fr.lr,
                (unsigned long)fr.cfsr, (unsigned long)fr.hfsr, (unsigned long)fr.aux, fr.tag);
    }

    app_init_hardware();
    app_create_tasks();
    sig_log("#RTOS,kernel=%s,heap_total=%u,heap_free_before_start=%u,tick_hz=%u",
            tskKERNEL_VERSION_NUMBER, (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize(),
            (unsigned)configTICK_RATE_HZ);
    vTaskStartScheduler();

    sig_fatal_early("scheduler returned");    /* only if the idle task could not be created */
    return 0;
}
