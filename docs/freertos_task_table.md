# FreeRTOS task table

FreeRTOS V10.3.1, preemptive, 1 kHz tick, heap_4 with 48 KB in CCM RAM, stack-overflow check method 2, malloc-failed hook enabled. The software-timer task is disabled because nothing uses timers.

| task | prio | stack (words / bytes) | activation | function | inputs | outputs / sync |
|---|---|---|---|---|---|---|
| `sensor` | 5 | 512 / 2048 | ICM-20608 DATA_RDY EXTI → task notification (100 Hz, timestamped in the ISR). Falls back to `vTaskDelayUntil` 10 ms if DRDY stops. | Read the IMU and validate it (bus error, stuck frame, gaps); fault state machine with re-init; AP3216C every 20th sample (5 Hz) → proximity gesture detector (`prox_ack.c`) → `gl_prox_ack`; raw stream in capture mode; gyro-bias estimation | DRDY notify; `I2C2` mutex | `q_imu` queue (64 × 24 B); `state` mutex; `q_ui` events; log stream |
| `infer` | 4 | 1024 / 4096 | each `q_imu` item; evaluates every 50 samples (0.5 s) once 200 are buffered | 2 s window → 12 features → decision tree + rule baseline → guardian logic; latency measurement | `q_imu` | `state` mutex (prediction, latency rings); `q_ui`; `q_evlog`; `#INF` log |
| `ui` | 3 | 1024 / 4096 | `q_ui` message or 20 ms timeout | Sole owner of user input/output: LCD, matrix, buzzer, LEDs, keys **and the IR receiver** (NEC decode of the ISR edge ring, remote pairing and control) and **IR alert relay** transmit; alert patterns, debounced short/long press, 4 screens, proximity-calibration wizard, operator prompts (`say`/`ask`) with key answers, sample→response and key→feedback timing | `q_ui` (events, key edges from EXTI ISRs, tests, prompts, cues, IR requests); IR edge ring (ISR → task) | `state` mutex; actuators |
| `health` | 2 | 1024 / 4096 | `vTaskDelayUntil` 100 ms | IWDG supervision (refresh only if all heartbeats arrived), AHT21 (2 s), MCU temp/VDDA (5 s), RTC (1 s), `#HEALTH`/`#ENV` every 10 s, module diagnostics; product records: W25Q event log (opt-in), **microSD CSV event/climate log**, **CAN/RS485 event + 5 s heartbeat telemetry** (opt-in) and RS485 `?STATUS` replies | `q_service`, `q_evlog`, `alive` event group, RS485 RX ring (ISR → task) | IWDG; `SPI2` mutex; SDIO; CAN1; USART6; log stream |
| `console` | 1 | 1024 / 4096 | log stream data or 10 ms timeout; TX completion notification | Drains the log stream to USART1 (interrupt-driven TX); parses commands; runs the golden self-test | `log` stream buffer (8 KB, mutex-protected writers), `rx` stream buffer (ISR → task) | `q_service`, `q_ui`, `state` mutex |
| `IDLE` | 0 | 160 / 640 | — | kernel idle | — | — |

## Synchronisation objects

| object | type | producers → consumers | purpose |
|---|---|---|---|
| `q_imu` | queue, 64 × `imu_msg_t` | sensor → infer | decouples sampling from inference; overflow is counted (`qfull`) |
| `q_ui` | queue, 16 | infer, sensor, console, key ISRs → ui | alerts, key edges, tests, screen/proximity-calibration requests |
| `q_service` | queue, 8 | console, ui → health | diagnostics, task report, log dump/erase |
| `q_evlog` | queue, 16 | any task → health | event records for the RAM ring and the opt-in flash log |
| `state` | mutex (priority inheritance) | all tasks | `g_app` shared state; held only for copies, never across I/O or sorting |
| `log` | mutex + stream buffer 8 KB | all tasks → console | whole lines or nothing; a full buffer drops the line and counts it |
| `rx` | stream buffer 256 B | USART1 ISR → console | command input |
| `i2c2`, `spi2` | mutexes | sensor/health | bus ownership (IMU + ALS + codec; flash + RW007) |
| `alive` | event group | sensor, infer, ui, console → health | heartbeat bits; IWDG is refreshed only when all four are set each second |

## Interrupts that interact with tasks

| ISR | priority | action |
|---|---|---|
| EXTI15_10 (PG12 ICM INT) | 5 | timestamp TIM2 → `vTaskNotifyGiveFromISR(sensor)` |
| USART1 | 6 | RX byte → `xStreamBufferSendFromISR`; TX empty → next byte / `vTaskNotifyGiveFromISR(console)` |
| DMA1_Stream5 | 6 | LED-matrix frame done (HAL callback) |
| EXTI0/1/4/9_5 (keys, IR RX) | 7 | keys: disable that key's line (debounce) → `xQueueSendFromISR(q_ui)`; IR RX (both edges): timestamp + level into a lock-free ring read by `ui` |
| USART6 (RS485) | 6 | received byte into a lock-free ring read by `health` (no kernel call) |
| TIM7 (IR transmit sequencer) | 4 | steps the NEC mark/space list, gating the TIM8 38 kHz carrier (no kernel call) |
| TIM6 | 3 | `HAL_IncTick()` only |

The microSD block transfers run their 512-byte FIFO phase inside `taskENTER_CRITICAL` (≈0.1–0.5 ms at 8 MHz), because SDIO hardware flow control is unusable on STM32F40x (errata ES0182). TIM6 and TIM7 stay above the masked level, so the HAL tick and IR timing continue.

## Measured stack/heap usage

Measured in the final hardware session (`evidence/hardware/final_20261118T081218Z/serial.log`, 25.6 min with sensing, inference, display refresh, proximity, diagnostics and the injected fault active): FreeRTOS heap 48 KB, minimum ever free **18,192 B**. Stack high-water marks (words, minimum over the run): sensor 242, infer 736, ui 741, health 572, console 612. CPU time: IDLE 94.2 %, sensor 4.4 %, ui 0.9 %, infer 0.1 %, health 0.1 %. 0 task stalls, 0 queue overflows, 0 watchdog resets. The static estimate (≈31 KB allocated, ≈17 KB left) matched the measurement.
