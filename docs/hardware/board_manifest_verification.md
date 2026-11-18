# Board definition verification (`boards/rt_spark_f407zg.json`)

A generic STM32F407 entry identifies the MCU, but it says nothing about the RT-Spark's crystals, wiring or memory use. Each item below states what was configured and the evidence behind it. **Offline** means verified from documents, the build or the ELF. **HW** means it still needs the board.

| item | configured | evidence | verified |
|---|---|---|---|
| MCU | `stm32f407zgt6`, `-DSTM32F407xx`, product line STM32F407xx | Chip marking in the vendor photo, schematic U1 "STM32F407ZGT6", vendor `.ioc` `Mcu.CPN=STM32F407ZGT6` | offline; DBGMCU IDCODE read at HW gate 1 |
| CPU / FPU | cortex-m4, `-mfpu=fpv4-sp-d16 -mfloat-abi=hard` (tools/build/fpu_flags.py) | RM0090; ELF attributes `Tag_FP_arch: VFPv4-D16`, `Tag_ABI_VFP_args: VFP registers` | offline |
| HSE | 8 MHz (`-DHSE_VALUE=8000000U`, hal_conf) | Schematic sheet 5 X2 "8Mhz"; `.ioc` `RCC.HSE_VALUE=8000000` | offline; `#CLOCK,hse_ok=1` at HW |
| PLL | M 4, N 168, P 2, Q 7 → 168/48 MHz; APB1 /4 (42 MHz), APB2 /2 (84 MHz); 5 wait states, scale 1 | `.ioc` RCC block; vendor `board.c` `SystemClock_Config`; RM0090 table 10 | offline; `#CLOCK` line at HW |
| HSE failure | falls back to HSI-based 168 MHz and reports `hse_ok=0` | `clock.c` | logic offline |
| LSE / RTC | 32.768 kHz; existing RTC configuration preserved (no backup-domain reset) | Sheet 5 X1; `rtc_clock.c` | HW |
| Flash | 1024 KB @ 0x08000000 | RM0090 2.3; datasheet DS8626 | offline (link map) |
| SRAM | 128 KB @ 0x20000000 (SRAM1 112 + SRAM2 16, contiguous, DMA-capable) | RM0090 2.3 | offline |
| CCM RAM | 64 KB @ 0x10000000, CPU only (not DMA) → FreeRTOS heap only | RM0090 2.3, 2.2 (bus matrix) | offline: `ucHeap` @0x10000000; DMA buffers `s_buf`, `s_sd_block` in SRAM |
| Stack (MSP) | top 0x20020000, 4 KB reserved | ELF vector[0] = 0x20020000 | offline |
| Vector table | 0x08000000; SVC/PendSV/SysTick → FreeRTOS port | ELF `.isr_vector` words 11/14/15 = port handlers | offline |
| HAL timebase | TIM6 1 kHz, NVIC priority 3 | `timebase.c` overrides HAL_InitTick | offline; runtime HW |
| Upload / debug | on-board ST-LINK/V2.1, OpenOCD `stm32f4x` target, `upload_protocol = stlink` | Schematic sheet 12 (STM32F103CB ST-LINK, SWD + NRST + VCP); vendor photo labels lower USB-C "ST-Link"; earlier Lab 3 log on COM3 showed the VCP working | HW gate 1 (read-only probe) |
| USB IDs | 0483:374B | ST-LINK/V2-1 default | HW (Device Manager) |
| Console | USART1 PA9/PA10 AF7 115200 8N1 | Schematic sheet 12 VCP_TXD/RXD ↔ UART1; vendor BSP default console | HW |
| Debug freeze | IWDG, TIM2, TIM6 stop when the core is halted | `main.c` DBGMCU_APB1FZ | HW (debug only) |

## Linker script

`boards/ldscripts/rt_spark_f407zg.ld` was checked against the ELF on 2026-11-17 (`arm-none-eabi-nm`/`objdump`):

- `.isr_vector` at 0x08000000, `.text`/`.rodata` in FLASH, `.data` loaded from FLASH into SRAM.
- `ucHeap` (48 KB) at 0x10000000 (`.ccmram`, NOLOAD).
- `.noinit` (fault record, boot counter) after `.bss`, so the startup code does not zero it.
- `._user_heap_stack` reserves 4 KB for the MSP; the newlib heap size is 0 and `_sbrk` asserts.

## Interrupt priorities (NVIC group 4)

| source | priority | FreeRTOS API in ISR |
|---|---|---|
| TIM6 (HAL tick) | 3 | none (allowed above max-syscall) |
| TIM7 (IR transmit sequencer) | 4 | none (allowed above max-syscall) |
| EXTI15_10 (IMU DRDY) | 5 | `vTaskNotifyGiveFromISR` |
| USART1, DMA1 Stream5 | 6 | `xStreamBufferSendFromISR`, `vTaskNotifyGiveFromISR` |
| USART6 (RS485 RX) | 6 | none (byte ring) |
| EXTI0/1/4/9_5 (keys, IR RX) | 7 | `xQueueSendFromISR` (keys); none (IR edge ring) |
| PendSV, SysTick | 15 | kernel |

`configMAX_SYSCALL_INTERRUPT_PRIORITY` is 5, so every ISR that calls the kernel is at priority 5 or lower (numerically ≥ 5).
