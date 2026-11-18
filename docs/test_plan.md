# Test plan

| requirement (assignment) | how it is measured | tool / phase | evidence file |
|---|---|---|---|
| Build reproducibility, zero warnings | `pio run -e rt_spark` and `-e rt_spark_debug` | `tools/run_offline_checks.py` | `evidence/offline/LATEST.md`, `build_rt_spark.log` |
| Exported C model == trained model | host-compiled firmware C vs Python on every held-out window, bit-exact features + class + baseline | `tools/ml/verify_c_model.py` | `evidence/offline/c_parity_<model>.md` |
| Exported model correct **on STM32** | `golden`: embedded held-out windows through features + tree on target, compared with Python | final_test P0 | `evidence/hardware/final_*/serial.log` (`#GOLDEN`) |
| Sensor/module bring-up, timestamps, units, errors | boot `#INIT` lines, `diag *`, operator checks | `tools/hw/bringup.py` | `evidence/hardware/bringup_*/summary.md` |
| Sampling interval | 10 s raw capture: median/min/max Δt, missing samples | bringup | same |
| ≥ 30 train / ≥ 10 test windows per class, separate sessions | trainer requirement check | `tools/ml/train.py` | `models/<id>/report.md` |
| Baseline vs model, confusion matrix, per-class P/R | held-out S04 | `train.py` | `models/<id>/report.md` |
| ≥ 20 independent trials per class | guided trials, trial-level confusion matrix | final_test P1 | `final_*/trials.csv`, `summary.md` |
| ≥ 50 timed inferences, median/p95 | every `#INF` line carries DWT cycles; on-board `lat` ring | final_test (whole run) | `summary.md` R2 |
| Sampling-to-response delay | `#RESP` newest-sample→actuation; `#INF` sample→decision; plus algorithmic delay discussion | final_test | R3 |
| Flash/RAM footprint, model size | linker report, `nm` of model tables | `run_offline_checks.py` | `offline_checks_*.json` |
| Heap and stack high-water marks | `#HEALTH` every 10 s, `#TASK` | final_test P5 | R10 |
| ≥ 15 min continuous; crashes, hangs, missed reads, unresponsive controls, false alerts | log duration, `#BOOT` count, IWDG stalls, `missed`, key events, rough alerts in non-rough trials | final_test | R4, P1 false alerts |
| ≥ 5 min offline | product has no network; PC network off; longest uninterrupted inference stretch | final_test | R5 |
| Controlled failure | injected IMU failure: alert, no predictions, console + keys still served, recovery | final_test P3 | R6 |
| Touchless acknowledgement (AP3216C proximity) | measured calibration (`prox_check.py`: raw PS hands away / hand near + wizard); 10 deliberate gestures on TEST alerts; hand held near during 3 alerts (expect 0 acknowledgements); 15 s hands away (expect 0 gestures); gestures during handheld trials counted | `prox_check.py`, final_test P2 | `prox_check_*/result.md`, R8 |
| Proximity gesture logic | uncalibrated never fires; already-near never fires; one-shot per approach; brief pass ignored; hysteresis; invalid readings ignored; alert-age and still-rough guards | host C tests | `evidence/offline/LATEST.md` |
| Button labels vs firmware names | each key pressed by its printed word, typed cross-check | `key_check.py` | `keymap_check/verify_*.md` |
| Privacy/safety/accessibility | design review + observed alert redundancy | report | `docs/product_design.md` |
| microSD product log | TEST_ALERT/ACK written as new CSV lines; lines written during the final run | bringup; final_test R11 | `summary.md` |
| IR remote control + relay | NEC self-echo decoded; paired remote key raises a TEST alert; relay frames counted | bringup | `bringup_*/summary.md` |
| CAN / RS485 telemetry | transmit counters while enabled; reception needs a partner (recorded as PARTIAL until then) | bringup | `bringup_*/summary.md` |
| NEC codec correctness | encode→decode round trip, ±150 µs jitter, extended address, repeat code, corrupted frame, random noise | host C tests | `evidence/offline/LATEST.md` |
| Session tools | full dry runs against the simulated board (`--sim`, outputs deleted, never evidence) | `capture.py`, `collect_all.py`, `bringup.py`, `final_test.py` | `docs/development_milestones.md` |

The test also covers conditions where the product can reasonably fail:

- the injected sensor failure
- STILL ↔ NORMAL ambiguity during slow handling
- rough motion briefer than half a window
- strong IR light making PS readings invalid, and a hand resting over the sensor while the board is held
- a USB cable tug during handling (avoided by procedure, noted as a risk)

## Improvements to validate with data

These are candidates only; they count as improvements only once results support them.

1. The one-shot re-arm gesture design versus a plain near-threshold trigger, compared on acknowledgements while a hand stays near (the final test logs every `#PROX,gesture` line with its reason).
2. The ROUGH-immediate plus majority-vote display versus raw per-window predictions, compared by the false-state-change rate in STILL trials. The per-window predictions are in `#INF`, so this needs no extra run.
3. Decision tree versus rule baseline on the independent S06 session and on the final trials.
4. Tree depth chosen by CV versus the depth-2 tree, compared on accuracy and footprint.
