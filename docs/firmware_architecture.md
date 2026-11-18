# Firmware architecture

## Stack and integration choices

- **PlatformIO** `platform = ststm32@19.7.1`, `framework = stm32cube` (STM32CubeF4 1.28.1, HAL 1.8.3), with the custom board `boards/rt_spark_f407zg.json` and linker script `boards/ldscripts/rt_spark_f407zg.ld`.
- **HAL configuration** is the project's own `include/stm32f4xx_hal_conf.h` (`custom_config_header = yes`), generated from ST's template so the diff stays reviewable.
- **FreeRTOS 10.3.1** is vendored in `lib/FreeRTOS-Kernel` (kernel + `GCC/ARM_CM4F` port + `heap_4`) and configured by `include/FreeRTOSConfig.h`. The CMSIS-RTOS wrappers are not used, so nothing duplicates the startup code, vectors or timebase:
  - The framework supplies `startup_stm32f407xx.s` and `system_stm32f4xx.c`. The project defines no `Reset_Handler` and no `SystemInit`.
  - `SVC_Handler`, `PendSV_Handler` and `SysTick_Handler` *are* the port's functions, via macros in FreeRTOSConfig.h. `stm32f4xx_it.c` does not exist here, and fault handlers live in `src/bsp/fault.c`.
  - The HAL tick runs on **TIM6** (`src/bsp/timebase.c` overrides `HAL_InitTick`), so SysTick belongs to the kernel only. TIM6 runs at priority 3, above the max-syscall level, so HAL timeouts keep counting inside FreeRTOS critical sections and before the scheduler starts. The port masks priority ≥ 5 between object creation and scheduler start.
  - `NVIC_PRIORITYGROUP_4` is set in `HAL_MspInit`, as required by the CM4F port.
- **FPU:** hard-float ABI is applied to every compiled unit by `tools/build/fpu_flags.py`. `-ffp-contract=off` keeps feature arithmetic free of FMA so it can be reproduced bit-exactly on the host.
- **No newlib heap or stdio:** formatting uses the project's reentrant `mini_printf.c`, and `_sbrk` asserts if anything calls malloc.

## Memory map

| region | content |
|---|---|
| FLASH 0x08000000 | vectors, code, rodata, golden vectors (~74 KB while the placeholder is embedded) |
| SRAM 0x20000000 (128 KB) | `.data`, `.bss` (DMA/ISR buffers: LED matrix 1 KB, SD block, console TX chunk, inference ring 2.4 KB), `.noinit` fault record, 4 KB MSP |
| CCM 0x10000000 (64 KB) | FreeRTOS heap 48 KB: TCBs, task stacks, queues, stream buffers. CPU-only: never a DMA source or target |

## Data flow and timing

1. The ICM-20608 samples at 1 kHz internally, with DLPF 41/44.8 Hz and an output rate of 100 Hz. DATA_RDY pulses PG12, and the EXTI ISR stamps TIM2 (1 MHz) and notifies `sensor`.
2. `sensor` reads 14 bytes over I2C2 at 400 kHz (~0.4 ms) and validates them. It forwards `imu_msg_t {seq, t_us, raw, flags, missed}` to `q_imu`. Invalid reads are flagged and zeroed, never passed off as data.
3. `infer` keeps a 200-sample ring. Every 50 samples it computes 12 float32 features (`src/core/features.c`), walks the decision tree (`src/core/model_infer.c`, at most depth + 1 comparisons), evaluates the rule baseline, and passes the result to the guardian logic. A window containing any invalid sample, or more than 2 missed samples, produces **no prediction**.
4. Guardian logic (`src/core/guardian_logic.c`) applies a 3-vote majority for the displayed state, shows ROUGH immediately, applies a 5 s rough-event cooldown, and handles TEST alerts, acknowledgement (key, proximity gesture, IR, console; source recorded) and the sensor-fault state. A proximity gesture acknowledges only an alert at least 1 s old and not while ROUGH is shown (`gl_prox_ack`).
5. `ui` actuates the buzzer, LED and matrix, then redraws the LCD banner. Two delays are measured: *newest-sample timestamp → actuation* (`#RESP`) and *key edge → chirp/LED* (`#KEY`).

Algorithmic delay: a decision covers the last 2 s, and decisions arrive every 0.5 s. The time from the *start* of a rough movement to the alert therefore includes up to 0.5 s of hop wait, plus the time until enough rough samples are in the window. This is separate from the firmware processing delay that `#RESP` measures.

## Fault handling

| fault | detection | response |
|---|---|---|
| IMU bus error / stuck frame | per sample | sample flagged; 10 consecutive → **SENSOR FAULT** alert (LCD, matrix, buzzer); bus recovery + re-init every 1 s; cleared after 20 valid |
| DRDY interrupt missing | 5 × 25 ms timeouts | switch to 10 ms `vTaskDelayUntil` polling, samples flagged `polled` |
| Missed samples / queue overflow | sequence gaps, `xQueueSend` failure | counted; windows with > 2 missed samples are rejected |
| I2C lock-up | HAL timeout/BERR/ARLO | 9-clock bus recovery and peripheral reset |
| AHT/ALS read error, CRC fail | per read | value marked invalid, counted |
| Task hang | missing heartbeat bit | IWDG not refreshed → reset after ~8 s (5.6–15 s over the LSI tolerance); stall mask stored in `.noinit` |
| HardFault/MemManage/Bus/Usage, stack overflow, malloc failure, `configASSERT` | Cortex-M / kernel hooks | record (PC, LR, CFSR, HFSR, task) in `.noinit` → reset → printed as `#FAULT,previous` at the next boot |
| HSE start failure | `HAL_RCC_OscConfig` | HSI-based 168 MHz, `hse_ok=0` reported |
| Log buffer full | stream-buffer space | whole line dropped and counted; the capture tool detects gaps from `seq` |
| microSD absent / not FAT / write error | `f_mount` / `f_open` / `disk_write` result | logged as `#SDLOG,...`, card unmounted, retried every 30 s; the card is never formatted |
| IR frame corrupted | NEC inverse-byte check, timing windows | frame dropped, `decode_errors` counted |
| CAN frame not acknowledged | mailbox TXOK clear, ESR LEC = ACK error | counted (`can_ack_err`); automatic retransmission is disabled, so nothing floods a bus |

## Module product/service functions (beyond the primary classifier)

| module | function | owner |
|---|---|---|
| microSD | `SIGUARD/EVENTS.CSV` (all product events, RTC time, uptime, boot count) and `SIGUARD/CLIMATE.CSV` (every 5 min: °C, %RH, lux, PS counts, humidity warning) | health task, `src/app/sd_logger.c` |
| IR receiver | any NEC remote paired (`ir learn`) to TEST ALERT / ACK / NEXT SCREEN | ui task, `src/drivers/ir_nec.c` |
| IR emitter | alert relay: NEC address 0x5A, cmd 1 rough, 2 test alert, 3 fault, 4 ack (on by default; `ir relay off`) | ui task |
| CAN1 / RS485 | opt-in event frames/lines + 5 s heartbeat; RS485 answers `?STATUS` | health task, `src/drivers/wired_link.c` |
| LCD + keys | operator prompts for guided sessions (`say`, `ask` → `#ANSWER`), buzzer cues (`cue`) | ui task |

## Controlled failure test

`fault imu on` forces the IMU read path to fail inside the sensor task. This exercises the real fault path (detection, alert, rejection of invalid windows, re-init attempts) without electrically disturbing the hardware. `fault imu off` restores it.

## Console protocol

Lines are `#TAG,...` (key=value where useful) and raw samples are `S,seq,t_us,ax,ay,az,gx,gy,gz,flags`. See `help` on the board, or `src/app/cli.c`.
