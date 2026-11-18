# Demonstration and explanation guide (≈ 7 minutes, live or video)

> Status: the demonstration video or live demonstration has **not been recorded yet**; it is the student's task. This guide is the script. Nothing in it is claimed as done.

**Setup.** The bare board (handheld prototype, no enclosure) sits on the table, LCD up, powered from the lower "ST-Link" USB-C with the power switch ON. A serial terminal is open at 115200 8N1 (`pio device monitor`). The firmware is build `Nov 18 2026 15:53:39`, model `dt-adfe15c9f6`; proximity is already calibrated (it stays calibrated in the RTC backup registers while the board is powered).

| # | show | do / say | expected |
|---|---|---|---|
| 1 | network disabled (30 s) | Switch Windows to airplane mode and show **all** adapters down (Settings → Network, or `Get-NetAdapter`). State that the firmware has no network stack and the RW007 is only presence-checked. | Everything below runs with the network off. This also closes the PC-isolation deviation of the final run, if it is filmed. |
| 2 | identity and RTOS (30 s) | `ver`, `tasks`, `heap` | model `dt-adfe15c9f6`, placeholder 0; 5 tasks + IDLE with priorities and stack high-water marks; heap minimum free |
| 3 | primary function (2 min) | Board on the table, then hold it and lift/carry calmly, then shake firmly with both hands (never drop it). | STILL (green) → NORMAL (cyan) → **ROUGH** (red), "!! ROUGH HANDLING", 3-beep rhythm, red flashing matrix, rough count +1 |
| 4 | acknowledgement (1 min) | Press **DOWN**. Press **UP** (TEST alert), take the hand away, then hold it over the AP3216C sensor for about 1 s. Keep the hand there and press UP again. | DOWN silences; the gesture acknowledges (double chirp, `#PROX,gesture,ack`); the second alert is **not** acknowledged while the hand stays (one-shot) |
| 5 | why it decided (30 s) | `verbose on` (then `verbose off`), `lat` | `#INF` lines: 12 features, tree class, confidence, rule baseline; inference median/p95 |
| 6 | controlled failure (1 min) | `fault imu on`, press a key, `status`; then `fault imu off` | "FAULT" box and SENSOR FAULT banner, buzzer; no predictions; keys and console still respond; recovery |
| 7 | diagnostics of verified modules (1.5 min) | **LEFT** to the SENSORS, SYSTEM and MODULES screens; **RIGHT** on MODULES (self-test: matrix colours); `diag all`; `diag leds`; `diag lcd` | live readings with units; module results (IMU, AP3216C, AHT21, W25Q64, RTC, IR self-echo with a card held above the IR LED, RW007 presence, ES8388, CAN loop-back, MCU ADC); matrix R/G/B/W + chase, LEDs red/blue, LCD colour bars. **Say out loud what is seen** - the operator confirmation of the LED matrix and LEDs was never recorded, so record it now (video) or have it observed live. |

Not shown, because the accessory is not available: a microSD card, an IR remote, a CAN/RS485 partner node. Say so if asked; do not simulate them.

## Explanation guide (every student must be able to answer)

| question | short answer | details |
|---|---|---|
| What problem, for whom? | immediate local warning when a carried item is handled roughly; handheld prototype, no enclosure | `docs/product_design.md` |
| Which sensors do what? | IMU = primary sensing (classification); AP3216C proximity = touchless acknowledgement (ALS shown); AHT21 = context/diagnostics; keys, LCD, matrix, LEDs, buzzer = interaction; W25Q, microSD, RTC = logging/timestamps; IR, CAN, RS485 = service functions | `docs/hardware/hardware_function_matrix.md` |
| How is the board configured? | custom board manifest, HSE 8 MHz → 168 MHz, CCM heap, HAL tick on TIM6, SysTick/PendSV for FreeRTOS | `docs/hardware/board_manifest_verification.md` |
| RTOS design? | 5 tasks (sensor P5 on DRDY, infer P4, ui P3, health P2 with IWDG supervision, console P1); queues, mutexes, event group, stream buffers | `docs/freertos_task_table.md` |
| Data and split? | 100 Hz, 2 s windows, 12 time-domain features; train S01–S03+S05, validation S04, independent test S06 (recorded after the freeze) | `docs/data_protocol.md`, report §4 |
| Why S04 is not the test set | its results led to the label-consistency rule, so it became development evidence; S06 was recorded after freezing | report §4 |
| Model vs baseline | depth-2 tree, 55 B; S06 0.970 vs 0.924 macro-F1 (3 windows of 66; correlated windows) | report §5 |
| How do you know the MCU runs the same model? | host C ≡ Python bit-exact on S04/S06; on-target golden 30/30 bit-exact | report §6 |
| Results and failures | 60 trials 0.933; 4/40 false rough alerts and 16/20 alerts inside the window - targets missed; PC isolation not shown in the final run | report §7, §10 |
| What would you improve? | more operators and NORMAL data near the NORMAL/ROUGH boundary; an alert window aligned to the operator's start; record in a mounted setting | report §9, §10 |
