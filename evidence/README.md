# Evidence index

Every serial log line carries a host UTC timestamp and a monotonic time (tab separated). `evidence/sim/` holds simulated dry runs only. It is git-ignored and is **never** evidence. Board flash backups (`*.bin`) are git-ignored; their SHA-256 is recorded in `gate1.json`. Host-PC user-profile paths were replaced by `<USERPROFILE>` / `<USER>` in `gate1_*/probe.log`, `gate1_*/backup.log`, `gate1_*/gate1.json` and `final_*/results.json` (privacy). Nothing else in these files was changed.

All hardware work used one RT-Spark board on 2026-11-18. The setup was the **bare board, no enclosure**; handling recordings were made with the board held by hand.

## Evidence requested by the activity sheet

| requested evidence | where |
|---|---|
| PlatformIO build | `offline/build_rt_spark.log`, `offline/build_rt_spark_debug.log`, `offline/LATEST.md` (0 warnings, Flash/RAM use) |
| PlatformIO upload | `hardware/upload_gate1.log` (placeholder firmware), `hardware/upload_gate2.log` (first trained build), `hardware/upload_gate2_handheld.log` (final build `Nov 18 2026 15:53:39`): guard lines, `Verified OK` |
| HAL initialisation, FreeRTOS scheduler start-up, task diagnostics | `hardware/bringup_20261118T050538Z/` (`#BOOT`, `#CLOCK`, `#INIT`, `#RTOS,scheduler_running`, `#TASK`, `#HEAP`); final run `final_20261118T081218Z/serial.log` (`#TASK`, `#HEAP`, `#HEALTH` every 10 s) |
| board identity / safe flashing | `hardware/gate1_20261118T050449Z/` (read-only probe: DEV 0x413, 1 MB, RDP level 0, backup SHA-256); `hardware/gate1_prelisten/`; `hardware/gate2*_pre/postupload_ver.log` |
| sensor and module tests | `hardware/bringup_20261118T050538Z/summary.md` (gate 1); `final_20261118T081218Z/results.json` → P4 `diag all` (10 PASS, microSD N/A); `hardware/keymap_check/verify_20261118T080937Z.md` (keys 4/4); `hardware/prox_check_20261118T081020Z/` (proximity raw readings + calibration) |
| sampling interval | `bringup_*/summary.md` (median 10,000 µs, 0 missing); every trial JSON in `../data/raw/` (rate, missing, invalid) |
| dataset, manifest, split | `../data/raw/S01`–`S06`, `../data/splits.json`, `../models/dt-adfe15c9f6/data_manifest.csv`, `../data/metadata_correction_20261118.json` |
| model metrics, baseline, confusion matrix | `../models/dt-adfe15c9f6/report.md` (validation S04); `offline/test_eval_S06_20261118T065156Z.md` (independent test S06, frozen model); `offline/post_capture_*.md`, `offline/round2_20261118T062639Z.log` |
| exported model = trained model | `offline/c_parity_dt-adfe15c9f6*.md` (host C vs Python, bit-exact, S04 and S06); on target: `final_*/serial.log` `#GOLDEN,SUMMARY` (30/30 agree, bit-exact) |
| freeze (no tuning on test data) | `../models/frozen.json`, `../models/freeze_history/` |
| on-device inference, latency, footprint | `final_20261118T081218Z/summary.md` R2/R3 (3,021 timed inferences, median 286 µs), `offline/LATEST.md` (Flash 166,500 B, RAM 26,448 B, model 55 B) |
| 20 product trials per class | `final_20261118T081218Z/trials.csv`, `summary.md` (confusion matrix, precision/recall) |
| 15 min continuous, controlled failure | `final_20261118T081218Z/summary.md` R4, R6; `results.json` P3 |
| offline operation | `final_20261118T081218Z/summary.md` R5 (24.7 min uninterrupted on-device inference). **Deviation:** the host adapter snapshot shows the PC was not fully disconnected (see the report, §7) |
| touchless acknowledgement | `final_20261118T081218Z/results.json` P2; `prox_check_20261118T081020Z/result.md` |
| operator runner log | `hardware/gate2_20261118T080936Z/runner.log`, `runner.json` |

## Folders

| folder | content |
|---|---|
| `offline/` | offline check records (`LATEST.md`, `offline_checks_<UTC>`), build logs, C/Python parity reports, post-capture and round-2 pipeline records, the one-shot S06 evaluation |
| `hardware/gate1_*`, `gate1_prelisten/` | read-only probe, backup checksum, passive serial listen before the first upload |
| `hardware/bringup_*` | gate-1 bring-up: boot log, diagnostics, sampling interval, golden self-test, latency |
| `hardware/keymap_check/` | key checks: gate 1 (ambiguous, kept for the record) and the final typed cross-check |
| `hardware/prox_check_*` | proximity raw readings and the board's measured calibration |
| `hardware/final_*` | integrated acceptance run (serial log, trials.csv, results.json, summary.md R1–R11) |
| `hardware/gate2_*` | runner log of the final session |
