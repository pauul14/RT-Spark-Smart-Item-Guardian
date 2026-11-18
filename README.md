# Smart Item Guardian

Repository: [pauul14/RT-Spark-Smart-Item-Guardian](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian).

A **handheld handling-warning prototype** built on the RT-Thread **RT-Spark** (STM32F407ZGT6). It classifies **STATIONARY / NORMAL_HANDLING / ROUGH_HANDLING** on the MCU with a compact decision tree and warns immediately on the LCD, LED matrix, LEDs and buzzer. An alert is acknowledged with the DOWN key or **touchlessly** by holding a hand over the AP3216C proximity sensor (a deliberate, one-shot gesture). It runs with no network at all.

> **No enclosure.** The prototype is the bare board. It has **not** been validated mounted to or inside an item (box, bag, case). All recordings were made with the bare board held by hand. The earlier box/lid concept and lid detection were removed on 2026-11-18.

It also:
- logs events and the climate to a **microSD** card;
- can be controlled from any **NEC IR remote** and relays its alerts as IR frames;
- can optionally send events over **CAN/RS485** to a hub.

BCA182 Laboratory Activity 4. Built with PlatformIO + STM32Cube HAL + FreeRTOS 10.3.1.

> **Status (2026-11-18, final hardware session done):** model `dt-adfe15c9f6` (S06 independent test 0.970 vs baseline 0.924) runs on the board in the frozen handheld firmware (build `Nov 18 2026 15:53:39`). Final run, 25.6 min: 60 trials with accuracy 0.933, on-board inference median 286 µs, 0 resets/stalls/missed samples, controlled failure handled, 10/10 touchless acknowledgements (0 while a hand stayed near), keys verified, RTC and CAN loop-back fixed. Open items, including 4/40 false rough alerts, PC network isolation not demonstrated, microSD/IR remote/CAN-RS485 receive and instructor exceptions, are listed in section 10 of the [report](docs/report/lab4_report.md). Evidence: `evidence/hardware/final_20261118T081218Z/`.

## Repository layout

| path | content |
|---|---|
| `platformio.ini`, `boards/` | build configuration, RT-Spark board definition, linker script (CCM heap) |
| `include/` | `FreeRTOSConfig.h`, generated `stm32f4xx_hal_conf.h` |
| `lib/FreeRTOS-Kernel/` | vendored FreeRTOS V10.3.1 (MIT), port ARM_CM4F, heap_4 |
| `lib/FatFs/` | vendored FatFs R0.12c (config `include/ffconf.h`, no mkfs) for the microSD log |
| `src/bsp/` | pins (from the schematic), clock tree, TIM6 HAL tick / TIM2 µs time, fault capture |
| `src/drivers/` | IMU, ALS/PS, AHT21, ST7789 LCD (FSMC), SK6805 matrix (TIM3+DMA), keys/buzzer/LEDs, I2C buses with recovery, SPI flash, RTC, microSD (SDIO), IR (NEC RX/TX), CAN/RS485 telemetry, module diagnostics, console UART |
| `src/core/` | portable code (also host-tested): features, tree engine, baseline, guardian logic, proximity gesture detector, NEC codec, latency stats, printf |
| `src/app/` | the five FreeRTOS tasks, CLI, golden self-test, event log (W25Q opt-in), microSD CSV logger |
| `src/model/` | **generated** model headers (tree, preprocessing contract, baseline, golden vectors) |
| `tools/ml/` | stdlib-only training/export (`train.py`), C parity check (`verify_c_model.py`), `post_capture.py`, `freeze.py`, one-shot `evaluate_test.py` |
| `tools/hw/` | hardware-session tools: `gate1_probe.ps1`, `bringup.py`, `capture.py` / `collect_all.py` / `round2.py`, `key_check.py`, `prox_check.py`, `final_test.py`, `gate2_run.py`, `analyze_run.py` (`--sim` dry runs) |
| `tools/run_offline_checks.py` | every offline check, writes `evidence/offline/` |
| `tests/` | Python unit tests, host C unit tests, parity harness |
| `data/`, `models/`, `evidence/` | dataset (S01–S06, bare board), model artifacts and freezes, evidence records |
| `docs/` | design, hardware matrix, board verification, architecture, task table, data protocol, training config, test plan, hardware sessions, demo guide, laboratory report, submission checklist |

## Build (offline)

```powershell
cd Smart-Item-Guardian
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $pio run -e rt_spark                 # release firmware (.pio/build/rt_spark/firmware.elf)
& $py tools\run_offline_checks.py      # unit tests, C parity, build, footprint, hygiene
& $py tools\ml\freeze.py --verify      # the build must match the frozen record before any upload
```

No third-party Python packages are needed: PlatformIO's Python 3.11 with pyserial is enough.

## Flash and run (hardware; approval required)

Read **[docs/hardware_sessions.md](docs/hardware_sessions.md)** first. The upload is guarded twice (`tools/build/upload_guard.py`): gate-1 probe/backup record, and the frozen build hashes. The final session is driven by one local runner that works with the PC offline:

```powershell
& $py tools\hw\gate2_run.py --port COM3    # identity check, button check, proximity calibration, network off, final test
```

The console is at 115200 8N1 on the ST-LINK virtual COM port (`pio device monitor`); type `help`.

## Machine-learning workflow

```powershell
& $py tools\hw\round2.py               # S05 capture -> train/validate/export/parity/build/freeze -> S06 -> one-shot evaluation
& $py tools\ml\post_capture.py         # dataset gate -> train -> quality gate -> C parity -> offline checks/build -> freeze
& $py tools\ml\evaluate_test.py        # frozen model on the independent test session (refuses if anything changed)
```

Split: train S01, S02, S03, S05; validation S04 (it informed the label-consistency rule); independent test S06.

## Console quick reference

| command | purpose |
|---|---|
| `ver`, `status`, `tasks`, `heap`, `cnt`, `lat` | identity, product state, RTOS stack/heap, fault counters, latency median/p95 |
| `diag all` / `diag <module>` | module diagnostics (`imu als aht flash sd rtc ir wifi codec can mcu i2cscan lcd matrix buzzer leds`) |
| `golden` | run the embedded held-out windows through the on-target model |
| `cap start <label> <session> <trial>` / `cap stop` | raw 100 Hz stream for data collection |
| `fault imu on/off` | controlled failure test |
| `alert test`, `ack`, `cal gyro`, `time set ...` | product controls |
| `prox`, `prox cal`, `prox clear` | proximity status, measured calibration wizard, forget calibration |
| `sd`, `sd on/off` | microSD CSV log status / enable (default on; never formats a card) |
| `ir`, `ir learn`, `ir relay on/off`, `ir clear`, `ir send <addr> <cmd>` | IR remote pairing/control and the IR alert relay |
| `wire`, `wire can on/off`, `wire rs485 on/off` | opt-in wired event/heartbeat telemetry (default off) |
| `say <s> <text>`, `ask <s> <text>`, `cue <name>` | operator prompts on the LCD (used by the session tools) |

Keys (printed labels = firmware names, verified 2026-11-18 with `tools/hw/key_check.py`, `evidence/hardware/keymap_check/verify_20261118T080937Z.md`):

| key | action |
|---|---|
| UP | raise a TEST alert |
| DOWN | acknowledge |
| LEFT | next screen |
| RIGHT | screen action (hold it on GUARD for the proximity calibration) |

Touchless acknowledgement: take the hand away, then hold it over the AP3216C sensor for about 1 s. It works only after a measured `prox cal`, only on an alert at least 1 s old, and a hand that stays near never triggers twice. When an `ask` prompt is on screen, UP / DOWN answer it instead of their normal action. A paired IR remote duplicates TEST ALERT, ACK and NEXT SCREEN.

## Troubleshooting

| symptom | check |
|---|---|
| No COM port / ST-Link device | Use the lower USB-C, latch the power switch ON, and use a data cable. |
| `pio run` cannot find the board | Run from the project root (`boards_dir = boards`). |
| Upload refused by `upload_guard` | Either no passing gate-1 record, or the build differs from `models/frozen.json` (`freeze.py --verify` names the file). |
| Host parity check cannot compile | It needs PlatformIO's `toolchain-gccmingw32`; the script puts its `bin` on PATH. |
| Board resets every ~8 s | The watchdog is firing: a task stopped checking in. Read `#FAULT,previous` and `#WARN,task_stall`. |
| `SENSOR FAULT` on the LCD | IMU reads failing: run `cnt` and `diag imu`, and `fault imu off` if a test left it on. |
| Hand gesture does nothing | `prox` shows `prox_cal=none` until `prox cal` succeeded; strong IR light can make PS invalid (`ps_valid=0`); gestures on alerts younger than 1 s or while ROUGH is shown are ignored (see `#PROX,gesture,<reason>`). |

## Documentation

| topic | document |
|---|---|
| product, states, acceptance criteria, risks, block diagram | [docs/product_design.md](docs/product_design.md) |
| hardware inventory and function matrix | [docs/hardware/hardware_function_matrix.md](docs/hardware/hardware_function_matrix.md) |
| board definition evidence | [docs/hardware/board_manifest_verification.md](docs/hardware/board_manifest_verification.md) |
| firmware architecture | [docs/firmware_architecture.md](docs/firmware_architecture.md) |
| FreeRTOS task table | [docs/freertos_task_table.md](docs/freertos_task_table.md) |
| data protocol | [docs/data_protocol.md](docs/data_protocol.md) |
| training configuration | [docs/training_configuration.md](docs/training_configuration.md) |
| test plan | [docs/test_plan.md](docs/test_plan.md) |
| hardware sessions | [docs/hardware_sessions.md](docs/hardware_sessions.md) |
| demonstration and explanation guide (demo not yet recorded) | [docs/demo_procedure.md](docs/demo_procedure.md) |
| laboratory report | [docs/report/lab4_report.md](docs/report/lab4_report.md) |
| submission checklist (requirements audit, open items) | [docs/submission_checklist.md](docs/submission_checklist.md) |
| evidence index | [evidence/README.md](evidence/README.md) |
| dependency versions | [docs/dependency_versions.md](docs/dependency_versions.md) |
| milestones | [docs/development_milestones.md](docs/development_milestones.md) |
| attribution | [ATTRIBUTION.md](ATTRIBUTION.md) |

## License

No project-level open-source licence has been selected for the project's original code. The proposed MIT licence was not approved and is not included. Third-party components keep their own licences; see [ATTRIBUTION.md](ATTRIBUTION.md).
