# Submission checklist - BCA182 Laboratory Activity 4

Audited against the activity sheet ("Design and Development of an Edge AI Consumer Product", 100 points) on 2026-11-18, using the existing evidence only. Status values: **met**, **partly met** (stated how), **open** (needs action), **instructor** (needs an instructor decision). Evidence paths: `evidence/README.md`.

## Mandatory software and configuration

| requirement | status | evidence |
|---|---|---|
| PlatformIO build and upload, `platform = ststm32`, `framework = stm32cube` | met | `platformio.ini`; `evidence/offline/build_rt_spark.log`; `evidence/hardware/upload_*.log` |
| verified board manifest / RT-Spark-specific board definition (pins, oscillator, memory, linker) | met | `boards/rt_spark_f407zg.json`, linker script; `docs/hardware/board_manifest_verification.md` |
| STM32Cube HAL/LL for peripherals | met | `src/bsp/`, `src/drivers/` |
| FreeRTOS explicitly integrated; tick, HAL timebase, IRQ priorities, heap consistent | met | `lib/FreeRTOS-Kernel/`, `include/FreeRTOSConfig.h`; HAL tick TIM6; `docs/hardware/board_manifest_verification.md`; scheduler start-up `bringup_*` |
| ST-LINK upload arrangement | met | `evidence/hardware/upload_*.log` |

## 1. Concept and design (10 pts)

| requirement | status | evidence |
|---|---|---|
| user, problem, environment, goal (one paragraph); ≥ 2 alternatives | met | report §1, `docs/product_design.md` |
| ≥ 3 classification states | met | STATIONARY / NORMAL_HANDLING / ROUGH_HANDLING |
| block diagram; component-to-function matrix with roles | met | report §1; `docs/diagrams/block_diagram.md`; `docs/hardware/hardware_function_matrix.md` |
| acceptance criteria, error types, measurement plan, privacy/accessibility/safety risks | met | `docs/product_design.md` (A1–A8 and measured outcome), report §8 |

## 2. Board audit and integration (15 pts)

| requirement | status | evidence / note |
|---|---|---|
| board revision, MCU, fitted modules, connectors, resources | met | `docs/hardware/hardware_function_matrix.md`, `board_manifest_verification.md` |
| HAL init + scheduler start-up demonstrated; platformio.ini, sources, build output, initial serial logs kept | met | `bringup_20261118T050538Z/serial.log` (`#RTOS,scheduler_running`), `gate1_prelisten/` |
| every available sensor read with timestamps, units, error states (console + display) | met | `#ENV`/`#DIAG` lines, SENSORS screen; bring-up and final `diag all` |
| every fitted UI module exercised | **partly met** | keys verified 4/4; LCD and buzzer confirmed by the operator; LED matrix and LEDs driven (`#DIAG,matrix/leds,DONE`) but **visual confirmation not recorded** → open item 3 |
| storage/communication verified where available | **partly met** | W25Q64 (read-only), RTC (LSE, time set/read) verified; microSD socket fitted but no card available; RW007 presence only → instructor |
| sampling interval, calibration/offset, init failures, absent hardware | met | 100 Hz median 10,000 µs, 0 missing; gyro bias; proximity calibration; RTC failure (gate 1) fixed; SD "no card" handled |
| integrated diagnostic/service mode for non-primary modules | met | `diag all`, MODULES screen, `sd`, `ir`, `wire` commands |

## 3. Data and model (15 pts)

| requirement | status | evidence |
|---|---|---|
| board-generated data; sampling, window, units, labels, procedure | met | `data/raw/`, `docs/data_protocol.md` |
| ≥ 30 train / ≥ 10 test windows per class, separate sessions, no leakage | met | train STILL 66 / NORMAL 77 / ROUGH 46; S06 test 22 per class; session split, freeze before S06 |
| invalid readings cleaned, exclusions recorded | met | `models/dt-adfe15c9f6/report.md` (label-consistency exclusions), trial exclusions in trial JSON |
| baseline comparison | met | rule baseline on S04 and S06 |
| held-out confusion matrix, per-class P/R, error and imbalance explanation | met | report §5, `evidence/offline/test_eval_S06_*.md` |
| model footprint and full firmware memory use | met | 55 B model tables, Flash 166,500 B, RAM 26,448 B, 48 KB heap |

## 4. Firmware with on-device inference (20 pts)

| requirement | status | evidence |
|---|---|---|
| task table; ≥ 3 tasks; queues/mutexes/event groups | met | `docs/freertos_task_table.md` (5 tasks) |
| validation, timestamping, invalid values rejected, fault handling, no busy-wait sampling | met | DRDY-driven sampling with `vTaskDelayUntil` fallback; R6 (0 predictions during the fault) |
| inference engine + weights + preprocessing in C on the STM32; model and compiler versions; correctness on the MCU | met | `src/core/`, `src/model/`; golden 30/30 bit-exact |
| local display of state/warning; ≥ 1 input control with feedback | met | GUARD screen; DOWN key / touchless gesture / UP test alert |
| every verified module accessible in one firmware | met | single firmware, diagnostics; exceptions → instructor |
| stack HWM, heap, shared-state protection, IRQ priorities, responsiveness under load | met | R10, `docs/freertos_task_table.md` measured section |
| network removed → inference and feedback continue | **partly met** | device has no network; 24.7 min uninterrupted on-device inference. In the final run the PC network was **not shown** to be disconnected → open item 2 |

## 5. Testing and evaluation (15 pts)

| requirement | status | evidence |
|---|---|---|
| test plan incl. failure conditions | met | `docs/test_plan.md` |
| ≥ 20 independent trials per class, not used for training | met | 60 trials, accuracy 0.933 (`final_*/trials.csv`) |
| ≥ 50 timed inferences, median, p95, sampling-to-response | met | n = 3,021, 286 / 288 µs; sample → alert 868 µs |
| Flash, heap, task stacks, overflow risks | met | report §6, §7 |
| ≥ 15 min continuous: crashes, hangs, missed reads, unresponsive controls, false alerts | met | 25.6 min, 0 resets/stalls/missed; **4/40 false rough alerts reported** (product target ≤ 2 not met) |
| ≥ 5 min offline + controlled failure; scheduler responsive | partly met / met | offline: see above; controlled failure: met |
| privacy, safety, accessibility; experimental-prototype statement | met | report §8 |
| dataset limitations, repeatable numbers, ≥ 2 improvements supported by tests | met | report §9 (label consistency; sensor-path efficiency), §10 |

Failed product targets are kept in the report, not removed: A1 16/20 alerts inside the window (target ≥ 18), A2 4/40 false alerts (target ≤ 2).

## Documentation and submission

| item | status |
|---|---|
| repository content: source, platformio.ini, board manifest, FreeRTOS config, drivers, build/flash steps, troubleshooting | met (`README.md`) |
| licence / attribution | original-code licence status documented: **not selected**; no unapproved MIT licence is published. Third-party licences and attribution remain in `ATTRIBUTION.md` |
| no passwords, API keys, personal data | met for tracked content: host profile paths redacted in evidence; `.vscode/` and flash backups git-ignored; operator ids anonymous |
| **public GitHub repository with meaningful commit history** | **met**: [public repository](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian), [commit history](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian/commits/main); published after student approval under `pauul14`, with no AI co-author trailers |
| laboratory report | met: `docs/report/lab4_report.md` (student details still blank, item 5) |
| data manifest + anonymised data + model artifacts + training config | met |
| evidence of build/upload, scheduler, diagnostics, inference, metrics, memory, latency, offline | met (offline with the deviation noted) |
| demonstration video or live demonstration with the network disabled | **open** (item 2), script in `docs/demo_procedure.md` |

## Submission closeout status

### Student actions and publication status
1. **Publication complete:** the reviewed source, data, model, documentation and evidence are published at [pauul14/RT-Spark-Smart-Item-Guardian](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian). Local and remote `main` were verified equal. Commits use `pauul14` and its GitHub no-reply address; there are no AI co-author trailers. Private flash backups, vendor PDFs/images and CLAUDE.md files are excluded.
2. **Record the demonstration** (video or live) per `docs/demo_procedure.md`, with all PC network adapters visibly disabled. This also provides the network-removed check that the final run did not fully demonstrate.
3. **Confirm the LED matrix and LEDs visually** during the demonstration (`diag matrix`, `diag leds`), and note it in the report §2 only after it has been observed.
4. **Licence status:** no project-level licence has been selected; MIT was proposed but not approved and is not published. A later licence choice is optional; third-party licences remain intact.
5. **Fill in student details**: name(s), section, date, and individual or group submission (report header; the activity-sheet cover fields).

### Instructor decisions (none approved so far)
6. RW007 Wi-Fi: presence check only (exception requested).
7. ES8388 codec / microphone / headphone jack: presence + register read only (exception requested).
8. USB-FS device port: unused (exception requested).
9. CAN and RS485 receive side: no partner node available; transmit side and CAN loop-back are verified.
10. microSD: the socket is fitted but **no card was available** (accessory); logging implemented but not verified - an exception, or a card to test with.
11. IR remote control: no remote available (accessory); IR emitter and receiver are verified by self-echo.
12. Expansion headers (PMOD, Pi, Spark-10P, RGB connector): empty connectors, nothing attached - confirmation only.
13. The CR1220 battery is not confirmed fitted (accessory); RTC backup-battery retention was not tested.
