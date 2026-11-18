/*
 * fault.c - reset cause, fault capture, early fatal path, FreeRTOS hooks.
 *
 * Faults are recorded into a .noinit record and the MCU is reset. The record
 * survives the reset (SRAM keeps its content) and is printed at the next boot
 * as "#FAULT,..." so a crash during the 15-minute run cannot go unnoticed.
 */
#include "bsp.h"
#include "board_pins.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

sig_reset_info_t g_reset;
static sig_fault_record_t s_fault __attribute__((section(".noinit")));
static uint32_t s_boot_count __attribute__((section(".noinit")));
static uint32_t s_boot_magic __attribute__((section(".noinit")));

void sig_reset_capture(void)
{
    uint32_t csr = RCC->CSR;
    g_reset.csr = csr;
    if (csr & RCC_CSR_IWDGRSTF)      g_reset.cause = "watchdog";
    else if (csr & RCC_CSR_WWDGRSTF) g_reset.cause = "window-watchdog";
    else if (csr & RCC_CSR_SFTRSTF)  g_reset.cause = "software";
    else if (csr & RCC_CSR_BORRSTF)  g_reset.cause = "power-on/brown-out";
    else if (csr & RCC_CSR_PORRSTF)  g_reset.cause = "power-on";
    else if (csr & RCC_CSR_LPWRRSTF) g_reset.cause = "low-power";
    else if (csr & RCC_CSR_PINRSTF)  g_reset.cause = "reset-pin";
    else                             g_reset.cause = "unknown";
    RCC->CSR |= RCC_CSR_RMVF;

    bool cold = (csr & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF)) != 0;
    if (cold || s_boot_magic != SIG_FAULT_MAGIC) {
        s_boot_magic = SIG_FAULT_MAGIC;
        s_boot_count = 0;
        if (cold) s_fault.magic = 0;      /* SRAM content is garbage after power-up */
    }
    s_boot_count++;
}

uint32_t sig_boot_count(void) { return s_boot_count; }

const char *sig_fault_type_name(uint32_t t)
{
    static const char *const n[] = {"none", "hardfault", "memmanage", "busfault", "usagefault",
                                    "stack-overflow", "malloc-failed", "assert", "task-stall"};
    return t < sizeof(n) / sizeof(n[0]) ? n[t] : "?";
}

void sig_fault_record(uint32_t type, const uint32_t *frame, uint32_t aux, const char *tag)
{
    s_fault.magic = SIG_FAULT_MAGIC;
    s_fault.type = type;
    s_fault.pc = frame ? frame[6] : 0;
    s_fault.lr = frame ? frame[5] : 0;
    s_fault.psr = frame ? frame[7] : 0;
    s_fault.cfsr = SCB->CFSR;
    s_fault.hfsr = SCB->HFSR;
    s_fault.mmfar = SCB->MMFAR;
    s_fault.bfar = SCB->BFAR;
    s_fault.aux = aux;
    memset(s_fault.tag, 0, sizeof(s_fault.tag));
    if (tag) {
        size_t n = strlen(tag);
        const char *t = n >= sizeof(s_fault.tag) ? tag + n - (sizeof(s_fault.tag) - 1) : tag;
        strncpy(s_fault.tag, t, sizeof(s_fault.tag) - 1);
    }
    s_fault.boot_count = s_boot_count;
}

bool sig_fault_take_previous(sig_fault_record_t *out)
{
    if (s_fault.magic != SIG_FAULT_MAGIC) return false;
    *out = s_fault;
    s_fault.magic = 0;
    return true;
}

/* ---- Blocking UART for the early/fatal path -------------------------------- */
void sig_uart_puts_blocking(const char *s)
{
    if (!(RCC->APB2ENR & RCC_APB2ENR_USART1EN) || !(USART1->CR1 & USART_CR1_UE)) return;
    while (*s) {
        uint32_t guard = 200000;
        while (!(USART1->SR & USART_SR_TXE) && --guard) { }
        USART1->DR = (uint8_t)*s++;
    }
    uint32_t guard = 200000;
    while (!(USART1->SR & USART_SR_TC) && --guard) { }
}

static void led_red_on(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Pin = LED_R_PIN, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                           .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(LED_R_PORT, &g);
    HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, GPIO_PIN_RESET);   /* active low */
}

void sig_fatal_early(const char *msg)
{
    __disable_irq();
    led_red_on();
    sig_uart_puts_blocking("\r\n#FATAL,");
    sig_uart_puts_blocking(msg);
    sig_uart_puts_blocking("\r\n");
    for (volatile uint32_t i = 0; i < 20000000u; i++) { }
    NVIC_SystemReset();
}

/* ---- Cortex-M fault handlers -------------------------------------------- */
void sig_fault_capture(const uint32_t *frame, uint32_t type)
{
    const char *tag = "isr";
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && (__get_IPSR() == 0 || frame == (const uint32_t *)__get_PSP())) {
        tag = pcTaskGetName(NULL);
    }
    sig_fault_record(type, frame, 0, tag);
    led_red_on();
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __BKPT(0);                     /* stop here when a debugger is attached */
    }
    NVIC_SystemReset();
}

/* Naked trampolines: basic asm only. r0 = stacked frame (MSP or PSP), r1 = type. */
#define FAULT_TRAMPOLINE(name, type)                    \
    __attribute__((naked)) void name(void)              \
    {                                                   \
        __asm volatile("tst lr, #4      \n"             \
                       "ite eq          \n"             \
                       "mrseq r0, msp   \n"             \
                       "mrsne r0, psp   \n"             \
                       "mov r1, #" #type "\n"           \
                       "b sig_fault_capture \n");       \
    }

FAULT_TRAMPOLINE(HardFault_Handler, 1)
FAULT_TRAMPOLINE(MemManage_Handler, 2)
FAULT_TRAMPOLINE(BusFault_Handler, 3)
FAULT_TRAMPOLINE(UsageFault_Handler, 4)

void NMI_Handler(void) { while (1) { } }
void DebugMon_Handler(void) { }

/* ---- FreeRTOS hooks ---------------------------------------------------- */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    taskDISABLE_INTERRUPTS();
    sig_fault_record(5, NULL, 0, name);
    led_red_on();
    NVIC_SystemReset();
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    sig_fault_record(6, NULL, (uint32_t)xPortGetFreeHeapSize(), pcTaskGetName(NULL));
    led_red_on();
    NVIC_SystemReset();
}

void sig_rtos_assert_failed(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    sig_fault_record(7, NULL, (uint32_t)line, file);
    led_red_on();
    NVIC_SystemReset();
}

/* newlib's malloc is never used (FreeRTOS heap_4 only). Fail loudly if it is. */
void *_sbrk(int incr)
{
    (void)incr;
    sig_rtos_assert_failed("_sbrk", __LINE__);
    return (void *)-1;
}
