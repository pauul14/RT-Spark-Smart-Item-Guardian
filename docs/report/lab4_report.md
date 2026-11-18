# BCA182 Laboratory Activity 4: Smart Item Guardian
### A handheld edge-AI handling-warning prototype on the RT-Thread RT-Spark (STM32F407ZGT6)

> **Status (2026-11-18, after the final hardware session).** All values below are measured unless marked otherwise; evidence paths are given per item. Items that remain unresolved, and instructor exceptions, are listed explicitly in section 10. Values from the synthetic placeholder model are **not** results.
>
> **Scope (revised 2026-11-18).** The prototype is the **bare RT-Spark board, with no enclosure**, and it has **not been validated mounted to or inside an item**. The original box/lid concept and lid OPEN/CLOSED detection were removed; the AP3216C proximity channel is used instead for **touchless alert acknowledgement**. Lid detection and enclosure behaviour are therefore not claimed anywhere in this report.

Student name(s) / section / date: *(to be filled by the student - not filled by the tools)*

**Contents (laboratory-report parts required by the activity sheet):** problem and design §1 · verified hardware inventory §2 · architecture / block diagram §1, §3 · FreeRTOS task table §3 · dataset collection §4 · model and baseline §5 · on-device deployment §6 · testing results §7 · privacy, safety, accessibility §8 · improvements §9 · limitations and unresolved items §10 · technical references §11. Evidence index: `evidence/README.md`; submission checklist: `docs/submission_checklist.md`.

---

## 1. Problem and design

**Target user and problem.** People who carry or move fragile or valuable items (a camera, a lab instrument, a sample kit) often learn only later that the item was handled roughly. They need an immediate, local warning while it happens.

**Product.** Smart Item Guardian is a handheld handling-warning prototype: the bare RT-Spark board, moved together with the item or held while the item is handled. It classifies handling locally as STATIONARY, NORMAL_HANDLING or ROUGH_HANDLING with a compact decision tree and warns immediately on the LCD, LED matrix, LED and buzzer. An alert is acknowledged with the DOWN key or **touchlessly**: a deliberate hand gesture over the AP3216C proximity sensor (hand away, then held near for 0.6 s; one-shot, so a hand that stays near never re-triggers; enabled only after a measured calibration). It counts and logs events, needs no network, and is controlled with four keys (UP raises a TEST alert).

**Alternatives considered:** a study-desk posture assistant, a home comfort assistant, and the original box guardian with lid context (removed: no enclosure was built or validated). See `docs/product_design.md`.

**States, acceptance criteria and risks:** see `docs/product_design.md`. The criteria tables A1–A8 are the documented acceptance criteria. There is no assignment-mandated accuracy figure.

**Block diagram** (Mermaid version in `docs/product_design.md`):

```
 SENSE                         PROCESS (STM32F407 + FreeRTOS)                       ACT              RECORD
 ICM-20608 accel+gyro 100 Hz -> sensor task P5 (validate, timestamp) -> infer task P4 -> ui task P3 -> LCD, LED matrix,  UART log, microSD CSV,
 AP3216C PS/ALS 5 Hz -------> proximity gesture detector             (2 s window,      |              LEDs, buzzer     W25Q log (opt-in), RTC
 AHT21 T/RH 0.5 Hz ---------> health task P2 (IWDG, env, diagnostics) 12 features,      |
 4 keys (EXTI), IR remote --> ui task P3                              decision tree,    v
                                                                      baseline) -> guardian logic (votes, cooldown, alerts, acks, faults)
```

**Component-to-function matrix** (every verified component with its role: primary sensing, context, user interaction, logging/communication, diagnostic/service): `docs/hardware/hardware_function_matrix.md`; summary in §2.

## 2. Verified hardware inventory

The pin map was derived from the vendor schematic (Rev HW1.1) and cross-checked against the vendor BSP. The table with per-module evidence is in `docs/hardware/hardware_function_matrix.md`.

| module | role | physical verification |
|---|---|---|
| ICM-20608-G | primary sensing | **PASS** (gate 1): WHO_AM_I 0xAF, DRDY interrupt, 100 Hz with median interval 10,000 µs and 0 missing samples in bring-up and in all 34 recorded trials |
| AP3216C | interaction: touchless acknowledgement (PS); ALS shown | **PASS** (final session): measured calibration hands away 0–10 counts, hand 2–5 cm over the sensor 1023 counts (16 readings each) → thresholds FAR ≤ 263 / NEAR ≥ 516 derived by the board (`evidence/hardware/prox_check_20261118T081020Z/`); 10/10 deliberate gestures acknowledged, 0 acknowledgements while a hand stayed near, 0 gestures with hands away, 0 gestures during the 60 handheld trials. The first calibration attempt failed (hand not over the sensor during the near phase) and stored nothing. Lid detection was removed from the product (gate-1 lid attempt failed, no box). |
| AHT21/AHT20 | context / diagnostics | **PASS** (31.4 °C, 81 % RH, status 0x18) |
| LCD, LED matrix, LEDs, buzzer | user interaction | LCD (ID 0x81B3) and buzzer: used for every prompt and cue in all sessions; operator confirmed the FAULT screen and buzzer during the controlled failure (final session). LED matrix and LEDs: driven throughout (`#DIAG,matrix/leds` at gate 1) but **no explicit operator confirmation was recorded** - open item |
| keys | user interaction | **PASS** (final session, `evidence/hardware/keymap_check/verify_20261118T080937Z.md`): printed UP/DOWN/LEFT/RIGHT = firmware UP/DOWN/LEFT/RIGHT, typed cross-check; the gate-1 ambiguity was an operator/key-identification issue, no remap; key-to-feedback 10–11 µs (n = 4) |
| W25Q flash | logging (opt-in) | **PASS**: JEDEC EF4017 = W25Q64, **8 MB** (the photo was right; the schematic's 16 MB is not fitted); read-only check |
| RTC/battery | logging/timestamps | **PASS** in the final session (`#DIAG,rtc,PASS,source=LSE,time_valid=1`); it failed at gate 1 (`source=none`) and was fixed in firmware (restart the selected clock without a backup-domain reset). Backup-battery retention across power-off not tested |
| microSD | logging: CSV event + climate log | function implemented; **not verified** (no card was inserted in any session: `#DIAG,sdcard,N/A`) |
| IR emitter / receiver | interaction: NEC remote control; communication: alert relay | emitter → receiver decoded self-echo **PASS** (gate 1 and final session; relay frames tx 76 / rx 76 during the final run); remote pairing **not verified** (no remote available) |
| CAN, RS485 | communication: opt-in event/heartbeat telemetry | CAN1 silent loop-back **PASS** in the final session (failed at gate 1; fixed); transmit side PARTIAL (gate 1: CAN frames without ACK - no partner node; RS485 lines sent); receive side needs a partner node (**pending instructor verification**) |
| RW007, ES8388, USB-FS, expansion headers | presence/diagnostic only, or unused | integration exception **pending instructor verification (not approved)** |

Board definition evidence (clock tree, memory map, vector table, IRQ priorities): `docs/hardware/board_manifest_verification.md`.

## 3. Firmware architecture

PlatformIO `ststm32@19.7.1` + `stm32cube` (HAL 1.8.3) + vendored FreeRTOS 10.3.1 (ARM_CM4F port, heap_4 in CCM RAM). The HAL tick runs on TIM6 and SysTick on the kernel. Details are in `docs/firmware_architecture.md`, and the task table with synchronisation objects is in `docs/freertos_task_table.md`.

| task | prio | stack | activation | sync |
|---|---|---|---|---|
| sensor | 5 | 2 KB | IMU DRDY interrupt, 100 Hz | q_imu, state mutex, i2c2 mutex |
| infer | 4 | 4 KB | every 50 samples | q_imu → q_ui, q_evlog |
| ui | 3 | 4 KB | events / 20 ms | q_ui (incl. key ISRs) |
| health | 2 | 4 KB | 100 ms periodic | IWDG, alive event group, q_service |
| console | 1 | 4 KB | stream buffer / 10 ms | log + rx stream buffers |

## 4. Dataset and signal processing

Data manifest: `models/dt-adfe15c9f6/data_manifest.csv` (per trial: session, label, split, samples, SHA-256); raw anonymised recordings `data/raw/S01`–`S06` (CSV + JSON metadata); split `data/splits.json`; setup correction `data/metadata_correction_20261118.json`; capture procedure `docs/data_protocol.md`; training configuration `docs/training_configuration.md`, `tools/ml/training_config.json`.

> **Recording condition (correction, 2026-11-18).** All training, validation and test recordings (S01–S06) were made with the **bare RT-Spark board held directly in the hand**, with no box or enclosure. The protocol texts and the original session metadata mention a box, a lid and re-seating the board; that was the plan, not what was done. The metadata were corrected (`data/metadata_correction_20261118.json`); the sensor readings are unchanged. Everything in sections 4 and 5 therefore describes **bare-board** handling. The product scope was later revised to exactly this setting (handheld, no enclosure); behaviour inside a box or mounted to an item was never recorded or tested and is not claimed.

The protocol is in `docs/data_protocol.md`: 100 Hz, ±8 g / ±1000 dps, 2 s windows, 1 s trimmed per trial end, safe actions only, and a session-based split. Final split (`data/splits.json`): **train S01, S02, S03, S05; validation S04; independent test S06**. All recordings are kept; the round-1 split is preserved in `data/splits_round1_20261118.json`.

| item | value |
|---|---|
| sessions / trials | 6 sessions, 34 kept trials, all 25 s, 2,507–2,514 samples, 0 missing, 0 invalid. S01–S04: 2 trials per class each; S05: 4 NORMAL trials (four different gentle movements); S06: 2 trials per class |
| excluded trials (kept on disk with reason) | 5, all in round 1: S01 ROUGH 01–04 and S02 NORMAL 01, rejected through a misread post-trial key prompt. None in S05/S06 (typed confirmation, all 10 trials confirmed) |
| training windows per class (≥ 30 required) | STILL 66, NORMAL 77, ROUGH 46 (after the label-consistency rule) |
| validation windows S04 (≥ 10) | STILL 22, NORMAL 11, ROUGH 14 (after the rule; 66 as collected) |
| independent test windows S06 (≥ 10) | STILL 22, NORMAL 22, ROUGH 22 - identical as collected and after the rule |
| excluded windows and reasons | 0 for sample quality in any session. Label-consistency rule: 72 windows in S01–S04 (S03 NORMAL_HANDLING_02 contains essentially no handling: 1/11 windows active), 0 in S05 and 0 in S06 |
| class balance | training set unbalanced (ROUGH smallest); macro-F1 is the primary metric |

**Label consistency and why S04 is validation, not test.** In round 1 (S01–S04) the per-window motion timelines showed that in almost every NORMAL/ROUGH trial the board was handled for about 10–14 s and then rested (or handling started late), so 4–8 of the 11 windows of a handling trial were physically stationary but carried the trial label. Trained on all windows, the tree reached only 0.653 LOSO-CV macro-F1 and split on calibration-level differences of `acc_mean` (S04 accuracy 0.439, macro-F1 0.349; model `dt-ac8173075c`, kept for reference). A rule fixed from the **training** data only now excludes a handling-labelled window when neither `gyro_max` nor `acc_std` exceeds the largest value seen in any training STATIONARY window (2.11 dps, 0.0115 g); windows are excluded, never relabelled, and listed per trial in the model report. Because S04's results led to this rule, S04 was re-designated as validation/development evidence, and a new independent test session (S06) was recorded **after** the model had been frozen.

**Round 2 procedure.** (1) S05: four NORMAL trials with varied gentle handling. (2) Training on S01–S03 + S05, validation on S04, export, C/Python parity, firmware build, then freeze: `models/frozen.json` hashes the model, thresholds, exported headers, training code/config, split and firmware image (frozen 2026-11-18 06:29:40 UTC, S06 not yet recorded). (3) S06: bare board, new spot and surface, 6 trials. (4) `tools/ml/evaluate_test.py` evaluated the frozen model once; the freeze was verified intact before and after, and retraining or re-freezing is now refused by the tools. Collection used LCD prompts with the action first and typed terminal confirmations (no board keys); a handling trial with < 80 % moving windows would have been redone automatically (none was: every S05/S06 handling trial had 11/11 moving windows, every STILL trial 11/11 still windows).

The features are 12 time-domain statistics of acceleration magnitude, jerk and angular rate (`docs/training_configuration.md`). Spectral features were not used. A 2 s window at 100 Hz gives 0.5 Hz bins, and shaking (3–5 Hz) is already separated from calm handling by jerk and gyro statistics. They can be added if the held-out errors show a need.

## 5. Model and baseline

A CART decision tree (stdlib Python) with depth and leaf size chosen by leave-one-session-out CV, compared against a four-threshold rule baseline tuned on the same training windows.

**Final model `dt-adfe15c9f6`** (frozen): depth 2, 5 nodes, 55 B of tables; max_depth 2 / min_samples_leaf 8 selected by LOSO CV on the training sessions (CV macro-F1 0.880; folds S01 0.861, S02 0.889, S03 0.783, S05 0.989).

```
if gyro_max <= 2.138 dps:            STATIONARY
elif acc_mean <= 1.0424 g:           NORMAL_HANDLING
else:                                ROUGH_HANDLING
```

**Independent test S06** (frozen model, evaluated once; as collected = filtered, n = 66, no window excluded):

| metric | decision tree | rule baseline |
|---|---|---|
| accuracy | **0.970** | 0.924 |
| macro-F1 | **0.970** | 0.924 |
| precision / recall STILL | 1.000 / 1.000 | 1.000 / 1.000 |
| precision / recall NORMAL | 0.917 / 1.000 | 0.905 / 0.864 |
| precision / recall ROUGH | 1.000 / 0.909 | 0.870 / 0.909 |
| confusion (rows true STILL / NORMAL / ROUGH) | [22 0 0] [0 22 0] [0 2 20] | [22 0 0] [0 19 3] [0 2 20] |

The 2 tree errors are the last two windows (w9, w10) of S06 ROUGH_HANDLING_01, predicted NORMAL (the baseline makes the same mistake): the shaking became less vigorous at the end of the trial. Report: `evidence/offline/test_eval_S06_20261118T065156Z.md`.

**Validation S04** (development evidence, used before the freeze): tree accuracy 0.851 / macro-F1 0.813 vs baseline 0.723 / 0.577 after the label-consistency rule (n = 47; tree confusion [22 0 0] [0 9 2] [0 5 9]); as collected 0.606 / 0.581 vs 0.515 / 0.422 (rest windows inside S04 handling trials are predicted STATIONARY, which is physically correct).

**Interpretation and limits.** STATIONARY is separated perfectly by angular rate. NORMAL vs ROUGH is decided by one intensity threshold on the mean acceleration magnitude (vigorous shaking raises it above 1 g); this is simple, explainable and cheap, but it encodes one operator's idea of "rough": S04, where the round-1 handling was gentler, has ROUGH recall 0.64. All data come from one operator handling the bare board (no enclosure); the S06 margin over the baseline is 3 windows of 66, and windows within a trial are correlated, so the result shows the tree is at least as good as the hand-tuned rules, not that it is decisively better. Mounted to or inside an item (padding, bag, case), the motion the sensor sees would be damped and filtered, so the bare-board thresholds (in particular `acc_mean` ≤ 1.042 g for NORMAL vs ROUGH) may not transfer; this was not tested. More operators and recordings in a mounted setting are the first improvements. The round-1 models `dt-ac8173075c` and `dt-d8b2af7075` are kept in `models/` with their reports.

## 6. Deployment and verification on the STM32

| item | value | status |
|---|---|---|
| model tables | 55 B (`dt-adfe15c9f6`, 5 nodes) | offline |
| ML code (features + tree + baseline) | 1,060 B | offline |
| golden vectors in flash | ≈ 74 KB (30 S04 validation windows) | offline |
| firmware flash / SRAM / CCM (successor build: proximity acknowledgement + GUARD-screen cleanup, build stamp Nov 18 2026 15:53:39) | 166,500 B (15.88 %) / 26,448 B (20.2 %) / 48 KB FreeRTOS heap (75 %), 0 warnings | offline; successor freeze in `models/frozen.json` (ML artifacts hash-identical to the original freeze, archived in `models/freeze_history/`); flashed 2026-11-18 (guarded upload, `Verified OK`, one reset; `evidence/hardware/upload_gate2_handheld.log`); board identity verified |
| host C vs Python parity | classes, 12 features bit-exact and baseline 66/66 on S04 and 66/66 on S06, re-checked with the successor sources | offline |
| on-target golden self-test | 30/30 S04 windows: on-target class = Python class, 12 features bit-exact (max |Δ| = 0); 24/30 equal to the true label (the model's S04 performance) | final session, `#GOLDEN` |
| on-board inference time (features + tree + baseline, DWT cycles) | median 286.1 µs, p95 287.8 µs, max 293.8 µs (n = 3,021); 47,674 cycles last window | final session R2 |
| compiler / model versions | GCC 7.2.1, HAL 1.8.3, FreeRTOS 10.3.1, model `dt-adfe15c9f6` | |

## 7. System testing (final hardware session, 2026-11-18)

Set-up: handheld prototype, bare board, no enclosure, not mounted to an item; firmware build `Nov 18 2026 15:53:39`, model `dt-adfe15c9f6` (placeholder 0), identity verified before the run. One continuous logged run of 25.6 min (`evidence/hardware/final_20261118T081218Z/`, runner log `evidence/hardware/gate2_20261118T080936Z/`). These trials were never used for training or tuning.

| test | result |
|---|---|
| 60 independent trials (20 per class), trial-level confusion | accuracy **0.933**. STILL 20/20; NORMAL 16/20 (3 → ROUGH, 1 → STILL: trial 51 showed no motion above the STILL threshold in any window); ROUGH 20/20. Precision / recall: STILL 0.952 / 1.000, NORMAL 1.000 / 0.800, ROUGH 0.870 / 1.000 |
| rough alerts (A1) | strict scoring window [start beep, end + 1 s]: **16/20**. The 4 others (trials 5, 7, 16, 26) were raised 0.2–1.6 s *before* the start beep because shaking began during the get-ready countdown, so every rough trial produced an alert, but 4 fall outside the window the protocol defines. Target ≥ 18/20 is **not met under the strict definition** |
| false rough alerts in STILL/NORMAL trials (A2) | **4/40** (NORMAL trials 33, 36, 59 with one ROUGH window ≈ 5 s in; trial 24 from a window overlapping the pick-up just before the start beep); STILL trials 0/20. Target ≤ 2/40 **not met** |
| ≥ 50 on-board inferences: median / p95 inference time | n = 3,021; median 286.1 µs, p95 287.8 µs |
| sampling-to-response delay (firmware) | newest sample → decision median 710 µs (p95 712); sample → alert actuation (buzzer/LED) median 868 µs (p95 870, n = 26); → LCD banner median 5.7 ms. Algorithmic delay: decisions every 0.5 s on a 2 s window |
| key-to-feedback latency | median 10.5 µs, max 11 µs (n = 4 key presses in the run) |
| heap minimum free, stack high-water marks | FreeRTOS heap 48 KB, minimum ever free 18,192 B; stack HWM (words) sensor 242, infer 736, ui 741, health 572, console 612; CPU: IDLE 94.2 %, sensor 4.4 %, ui 0.9 % |
| 15 min continuous: resets, stalls, missed reads | 25.6 min, 0 resets, 0 task stalls, 0 missed samples, 0 queue overflows, 0 log drops, 0 I²C errors; 2,111 invalid samples, all inside the injected 20 s fault (0 before it) |
| ≥ 5 min offline inference | longest uninterrupted on-device inference 24.7 min; the device has no network stack (RW007 only presence-checked, never joins a network). **Protocol deviation:** the PC's adapters reported *Up* at the start and one adapter was still *Up* at the end although the operator answered "network off" both times, so isolation of the logging PC is **not** demonstrated; the offline claim rests on the device itself |
| controlled failure (injected IMU read failure, 20 s) | fault raised after 0.109 s; 0 valid predictions during the fault; console answered 10/10 status commands and 4 key presses were served; cleared on recovery, valid inference again after 2.77 s; operator confirmed the FAULT screen and buzzer |
| touchless acknowledgement (A6) | 10/10 deliberate gestures acknowledged (median cue → acknowledgement 1.19 s, incl. the operator's reaction); hand held near while 3 TEST alerts were raised: 0 acknowledgements (one gesture logged as `alert_too_new`, then latched, as designed); 15 s hands away: 0 gestures; handheld trials: 0 gestures |
| button labels vs firmware key names | 4/4 match (typed cross-check) |
| module diagnostics in the same firmware | 10 PASS, 0 FAIL: IMU, AP3216C, AHT21 (31.5 °C, 82 % RH), W25Q64, RTC (LSE), IR self-echo, RW007 presence, ES8388, CAN1 loop-back, MCU ADC (3.307 V); I²C scan 0x10/0x1E/0x68; microSD N/A (no card) |

## 8. Privacy, safety, accessibility

| aspect | consideration | how it was addressed / assessed |
|---|---|---|
| privacy | motion and proximity data could reveal behaviour | no network stack, no cloud, no camera or microphone use; data stay on the device and the USB log; operators are anonymous ids (P1); host-PC profile paths were redacted from the published evidence |
| safety | rough handling could damage the board or injure | only gentle, controlled simulations (no drops, throws or strikes), USB cable kept slack; the product is an **experimental prototype**, not a certified shock, drop or tamper detector |
| accessibility | alerts must not depend on one sense; hands may be busy | every alert is redundant (text, colour, flashing, distinct buzzer rhythm); large 24×32 px state word; acknowledgement by key **or** touchless gesture (measured 10/10, §7); limitation: no haptic output |

## 9. Improvements supported by results

1. **Label consistency for handling windows.** Excluding handling-labelled windows without motion raised training-only LOSO-CV macro-F1 from 0.653 to 0.874 (round 1). The model trained this way reached 0.970 accuracy / macro-F1 on the independent S06 session, against 0.924 for the rule baseline.
2. **Sensor-path efficiency (firmware fixes between gate 1 and the final session).** On hardware, the sensor task fell from 57.9 % to 4.4 % CPU, and on-board inference (features, tree and baseline) from a 631 µs to a 286 µs median. Both builds run a 5-node, depth-2 tree, so the model size is unchanged; the speed-up comes from the feature/IMU code (inlined `vsqrt`, reuse of the pass-1 magnitudes, sensor read path). The gate-1 build carried the placeholder tree.
3. **Typed key verification.** The gate-1 key results were ambiguous. A per-key typed cross-check (printed word pressed versus firmware name) resolved the question without a remap: all four labels match.
4. **One-shot proximity gesture.** Measured behaviour: 10/10 deliberate gestures acknowledged, 0 acknowledgements with a hand held near, 0 with hands away, and 0 during handheld trials. No A/B comparison against a plain threshold trigger was run, so this is evidence of the chosen design, not of a measured improvement over an alternative.

## 10. Limitations

- Rough handling is simulated by shaking and jerking. Real drops and impacts were not recorded, so model behaviour for them is unknown.
- The data come from one board, one operator and six placements, so generalisation is limited. NORMAL vs ROUGH is an intensity threshold learned from that operator.
- The label-consistency rule was introduced after the first held-out evaluation; this is why S04 was re-designated validation and S06 was recorded after the freeze. S06 is the only independent test result (6 trials, 66 correlated windows).
- **No enclosure; not validated mounted to an item.** The prototype is the bare board. Lid detection was removed and is not claimed; behaviour inside a box, bag or case, or fixed to an item, was never recorded or tested.
- The model was trained and tested only on bare-board recordings (held by hand).
- Touchless acknowledgement depends on a per-device measured calibration and on lighting (strong IR light invalidates PS readings); while the board is held, fingers near the sensor can produce gestures - every gesture is logged with its reason, and the final test counts them during handheld trials.
- USB tethering limits realistic carrying scenarios.

### Unresolved items (explicit)

| item | status |
|---|---|
| A1 rough alerts within the protocol window | 16/20 strict (target ≥ 18); the 4 others fired 0.2–1.6 s before the start beep |
| A2 false rough alerts | 4/40 (target ≤ 2) - NORMAL vs ROUGH intensity overlap; more operators/data needed |
| offline test: PC network isolation | not demonstrated (adapters *Up*); device-side offline operation 24.7 min |
| LED matrix / LEDs visual confirmation | not recorded by the operator |
| microSD logging | not verified (no card) |
| IR remote pairing/control | not verified (no remote) |
| CAN / RS485 receive side | needs a partner node - **instructor verification pending** |
| RW007, ES8388, USB-FS, expansion headers | presence/diagnostic only or unused - **integration exception pending instructor verification (not approved)** |
| RTC backup-battery retention | not tested |
| mounted / enclosed use | not validated (handheld prototype only) |
| demonstration video / live demonstration with network disabled | not yet recorded (script: `docs/demo_procedure.md`) |
| public GitHub repository with commit history | **published** after student approval: [pauul14/RT-Spark-Smart-Item-Guardian](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian), [commit history](https://github.com/pauul14/RT-Spark-Smart-Item-Guardian/commits/main); local and remote `main` verified equal |
| licence | no project-level licence selected; unapproved MIT proposal omitted from publication; third-party licences retained |
| student details, individual/group arrangement | to be filled by the student |

Full requirements audit against the activity sheet: `docs/submission_checklist.md`.

## 11. References

1. PlatformIO. STM32Cube framework: https://docs.platformio.org/en/stable/frameworks/stm32cube.html; ST STM32 platform: https://docs.platformio.org/en/latest/platforms/ststm32.html
2. FreeRTOS kernel documentation (V10.3.1): https://www.freertos.org/Documentation/RTOS_book.html
3. STMicroelectronics: RM0090 (STM32F4 reference manual), DS8626 (STM32F407 datasheet), ES0182 (errata), STM32CubeF4: https://www.st.com/en/embedded-software/stm32cubef4.html
4. RT-Thread RT-Spark schematic Rev HW1.1 and BSP `bsp/stm32/stm32f407-rt-spark` (wiring/pin-out reference only).
5. Datasheets: TDK ICM-20608-G, Dyna-Image AP3216C, Aosong AHT21, Sitronix ST7789V, Winbond W25Q64 (fitted; the schematic lists W25Q128), Everest ES8388.
6. ChaN, FatFs R0.12c (vendored, see `ATTRIBUTION.md`).
