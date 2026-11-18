# Hardware inventory and component-to-function matrix

Pins come from the vendor schematic (Rev HW1.1); sheet numbers follow the title-block "Id". The **Physical status** column records the hardware-session results (gate 1 and the final session, 2026-11-18) (evidence: `evidence/hardware/bringup_*`, `final_20261118T081218Z`, `prox_check_*`, `keymap_check`).

Role key: **P** = primary sensing, **C** = context sensing, **U** = user interaction, **L** = logging/communication, **D** = diagnostic/service.

| component | interface / pins (sheet) | role | integration in firmware | diagnostic | physical status |
|---|---|---|---|---|---|
| STM32F407ZGT6, 168 MHz | HSE 8 MHz X2, LSE 32.768 kHz X1 (5) | — | `clock.c` HSE→PLL 168 MHz, HSI fallback | `#CLOCK` boot line, `diag mcu` (die temperature, VDDA) | **verified**: HSE ok, SYSCLK 168 MHz (`#CLOCK`), MCU ADC 3.307 V |
| ICM-20608-G IMU | I2C2 PF1/PF0 @0x68, INT PG12 (6) | **P** | `task_sensor.c` 100 Hz on DRDY → `task_infer.c` model | `diag imu`, WHO_AM_I + config read-back | **verified**: WHO_AM_I 0xAF, DRDY 100 Hz, 0 missed samples in 25.6 min |
| AP3216C ALS/PS/IR | I2C2 @0x1E, INT PG13 (6) | **U** | touchless alert acknowledgement: deliberate one-shot hand gesture on the PS channel (`prox_ack.c`), thresholds only from the measured `prox cal` wizard; ALS shown on the SENSORS screen | `diag als`, `prox`, wizard `prox cal` | **verified** (final session): PS away 0–10 / hand 1023, measured thresholds 263/516; 10/10 gestures, 0 while held near, 0 hands away. Lid detection removed (no enclosure) |
| AHT21 T/RH (photo says AHT20) | soft-I2C PE0/PE1 @0x38 (6) | C / D | storage climate on GUARD screen, HUMID advisory at ≥70 % for 1 min | `diag aht`, CRC-checked reads | **verified**: 31.5 °C, 82 % RH, CRC ok, 0 errors |
| ST7789V3 LCD 240×240 | FSMC NE3 PG10, A18 PD13, D0–7, RST PD3, BL PF9 (8) | **U** | state, proximity status, alerts, counts, 4 screens | ID 0x81B3 read, `diag lcd` colour bars | **verified**: ID 0x81B3; prompts read by the operator in every session; FAULT screen confirmed |
| 19 × SK6805 LED matrix | PA7 TIM3_CH2 + DMA1 S5, ~OE PF2 (6) | **U** | state colour, flashing alerts, capture indicator | `diag matrix` R/G/B/W + chase | driven (`#DIAG,matrix,DONE`); **operator confirmation not recorded** |
| LEDs D4 (red / "blue") | PF12 / PF11, active low (6) | U | red = alert, blue = heartbeat | `diag leds` | driven (`#DIAG,leds,DONE`); **operator confirmation not recorded** |
| Active buzzer BUZ1 | PB0 via SS8050, active high (6) | **U** | alert rhythms, key chirp | `diag buzzer` | **verified**: start/stop cues used in all sessions; buzzer confirmed during the controlled failure |
| 4 arrow keys | PC5 UP, PC1 DOWN, PC0 LEFT, PC4 RIGHT, to GND (6) | **U** | test alert, acknowledge, screen, action / proximity calibration | `#KEY` lines with feedback latency | **verified**: printed label = firmware name for all 4 (typed cross-check); key→feedback 10–11 µs |
| Reset key SW6 | NRST | — | hardware reset (firmware cannot read it) | — | not tested (hardware reset; not used) |
| W25Q128 SPI flash (photo: "8MB") | SPI2 PB13/PC2/PC3, CS PB12 (7) | **L** | opt-in event log, last 64 KB only, never overwrites foreign data | `diag flash` JEDEC ID, SR1, read-only | **verified**: JEDEC EF4017 = W25Q64, 8 MB (read-only); event log not enabled |
| microSD socket | SDIO PC8–PC12, PD2, CD PF3 (7) | **L** | **product log**: `SIGUARD/EVENTS.CSV` (every event, RTC-stamped) + `SIGUARD/CLIMATE.CSV` (T/RH/light/PS counts every 5 min) on a FAT card; never formats, open-append-close per record (`src/app/sd_logger.c`, FatFs R0.12c) | `sd` status, `diag sd` (read-only block 0) | **not verified**: no card inserted (`#DIAG,sdcard,N/A`) |
| RTC + CR1220 holder | LSE X1, VBAT (4/5) | L | timestamps for events/diagnostics; proximity calibration and option flags in backup registers | `diag rtc`, `time set` | **verified**: LSE running, time set/read (final session); battery retention not tested |
| RW007 Wi-Fi module | SPI2 + CS PF10, INT PG11, RST PG15 (10) | D | **not used**: the product is offline by design | `diag wifi`: reset pulse + INT activity only | presence/boot signal PASS; exception pending instructor verification |
| IR emitter / receiver | PB1 (TIM8_CH3N) / PF8 IRM-H638T (6) | **U / L** | **remote control**: any NEC TV remote paired to TEST ALERT / ACK / NEXT SCREEN (`ir learn`); **alert relay**: NEC frames (addr 0x5A) sent on rough/test/fault/ack for a companion receiver (`src/drivers/ir_nec.c`, decoder host-tested) | `diag ir`: own TEST frame decoded by own receiver (reflector), `ir` status | self-echo **verified** (76/76 relay frames); remote control not verified (no remote) |
| ES8388 codec, mic, headphone jack | I2C2 @0x10, I2S3 (9) | D | none in the product | `diag codec`: ACK + register read (no writes) | ACK + register read PASS; exception pending instructor verification |
| CAN transceiver TJA1042 | CAN1 PB8/PB9, CN8 (11) | L | **opt-in wired telemetry** (`wire can on`): event frames 0x5A0+type and 5 s heartbeat 0x5AF at 500 kbit/s (`src/drivers/wired_link.c`) | `diag can` silent loop-back; `wire` shows TX ok / ACK errors | MCU loop-back **PASS** (final); bus TX without partner = no ACK (gate 1); receive pending instructor verification |
| RS485 SP3485 | USART6 PG14/PG9, RE/DE PG8 (11) | L | **opt-in wired telemetry** (`wire rs485 on`): `$SIGEV`/`$SIGHB` lines with NMEA checksum, answers `?STATUS` | `wire` shows lines sent / bytes / queries received | TX lines sent (gate 1); receive needs a partner - pending instructor verification |
| USB-FS device port (upper USB-C) | PA11/PA12 (11) | — | not used | — | — |
| ST-LINK/V2.1 (lower USB-C) | SWD, NRST, VCP → USART1 PA9/PA10 (12) | L | console 115200 8N1, flashing | `ver`, logs | **verified**: console/log and all uploads |
| PMOD ×2, Raspberry-Pi header, Spark-10P, RGB LED connector | expansion (11) | — | nothing attached → not integrated, as the assignment allows | — | inventory to confirm "nothing attached" |

### Integration status and exceptions (none approved)

The assignment allows a component to stay out of the product function only with an **instructor-verified exception**, and states that a connector or unsupported optional accessory is not an installed sensor. Presence-only or loop-back checks are **not** approved exceptions. Classification below: **fitted** = component soldered on the board; **connector** = socket/header with nothing fitted; **accessory** = external item not available in this kit. Record who confirmed an exception, when, and the wording.

| module | kind | product / service function now in firmware | what limits verification | status after the final session |
|---|---|---|---|---|
| microSD | connector (TF socket, card detect fitted); card = accessory | CSV event log + climate log on a FAT card | no card was available in any session | function implemented; socket/card detect read (`#DIAG,sdcard,N/A` = no card); **logging not verified** - verify with a card or instructor decision |
| IR emitter / receiver | fitted; remote = accessory | NEC remote control (pairing) + IR alert relay; decoded self-echo test | no IR remote available | emitter → receiver self-echo **verified** (relay frames 76/76); remote pairing/control **not verified** (accessory unavailable) |
| CAN (TJA1042) | fitted; bus partner = accessory | opt-in event/heartbeat telemetry | reception needs a second node or USB-CAN adapter | MCU silent loop-back **verified**; bus TX without partner = no ACK (expected); **receive side: instructor decision** |
| RS485 (SP3485) | fitted; bus partner = accessory | opt-in event/heartbeat telemetry + `?STATUS` query | the board cannot observe its own RS485 output | TX lines sent (gate 1); **receive side: instructor decision** |
| RW007 Wi-Fi | fitted | presence/boot-signal check only | product is offline by design; RW007 SPI protocol stack not integrated | presence PASS; **exception requested, not approved - instructor decision** |
| ES8388 codec / mic / jack | fitted (codec); jack = connector | I2C presence + register read only | no audio role; an alert tone would need I2S3 on pins shared with JTDO/JTDI | ACK + register read PASS; **exception requested, not approved - instructor decision** |
| USB-FS device port | connector (MCU USB peripheral) | unused | a USB CDC export port is possible but not implemented | **exception requested, not approved - instructor decision** |
| expansion headers (PMOD, Pi, Spark-10P, RGB connector) | connectors (nothing attached in this kit) | none | no modules supplied | not installed sensors; **instructor confirmation that nothing is attached** |
