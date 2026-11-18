# Data collection protocol

## Signals and sampling

| item | value | why |
|---|---|---|
| sensor | ICM-20608-G on the RT-Spark (board data only; no phone data) | primary modality |
| ranges | ±8 g (4096 LSB/g), ±1000 dps (32.8 LSB/dps) | vigorous shaking stays inside range; normal handling keeps resolution |
| output rate | 100 Hz (SMPLRT_DIV 9), DLPF 41 Hz gyro / 44.8 Hz accel | anti-aliasing below the 50 Hz Nyquist |
| timestamps | per-sample 1 MHz TIM2 stamp taken in the DRDY ISR, plus sequence number | detects gaps and jitter |
| units in features | g, g/s, dps (after gyro bias removal) | |
| window | 2.0 s = 200 samples; training windows non-overlapping; on-line hop 0.5 s | a 2 s window holds several shake cycles and one lift/set-down |
| trimming | 1.0 s removed at both ends of each trial | removes the operator's start/stop transitions |
| exclusions | any invalid sample, or > 2 missed samples in a window; operator-rejected trials; NORMAL/ROUGH windows with no handling motion (label consistency: `gyro_max` and `acc_std` both ≤ the maxima of the training STATIONARY windows; see `tools/ml/sigml/dataset.py`, enabled in `training_config.json`) | logged in `report.json` → `exclusions` / `label_consistency`; per-trial table in `report.md` |
| handling coverage | NORMAL/ROUGH: keep handling the board until the stop beep - rest periods inside a handling trial are excluded from training | `capture.py` prints `handling coverage x/11` after each trial and warns on the LCD below 80 % |
| gyro bias | estimated by the board over 2 s of stillness (`cal gyro`), recorded in each trial's JSON and used by the features | |

## Labels and safe actions

Each recording trial has one label. The protocol shown by `tools/hw/capture.py` is:

- **STATIONARY**: the board rests on the table and nobody touches it or the table.
- **NORMAL_HANDLING**: calm two-hand handling for the whole trial: lift, carry while walking at normal pace, turn to look, set down gently, slide across the table.
- **ROUGH_HANDLING (safe simulation)**: hold firmly with both hands; shake vigorously (≈3–5 shakes/s) for 2–3 s, pause, jerk-and-stop, twist fast, and repeat. **Never drop, throw, strike or bang.** The USB cable stays slack and is held together with the board. Stop immediately if the cable tightens.

All recordings use the bare board held by hand (no enclosure); see the correction below.

> **Correction 2026-11-18:** S01–S06 were all recorded with the bare board held directly by hand, with no box or enclosure, although the session texts below and the original metadata mention a box. Metadata were corrected with the logged texts kept (`data/metadata_correction_20261118.json`); the CSV readings are unchanged.

## Sessions and split (leakage control)

A **session** is one recording session: the box is set up again, the placement and surface are written in `session.json`, and the trials run in random order. The split is by session (`data/splits.json`), so neighbouring or overlapping windows of one trial can never sit on both sides.

| split | sessions | per class target | trials |
|---|---|---|---|
| train | S01, S02, S03 (vary placement/surface), S05 (NORMAL only, 4 varied gentle movements) | ≥ 30 windows (actual after exclusions: STILL 66, NORMAL 77, ROUGH 46) | `capture.py --trials-per-class 2 --seconds 25`; S05 `--labels NORMAL_HANDLING --trials-per-class 4` |
| validation | S04 (round-1 held-out session; its results led to the label-consistency rule, so it is development evidence only) | ≥ 10 windows | same |
| independent test | S06 (recorded after the model was frozen; new spot and surface, board re-seated) | ≥ 10 windows (actual 22 per class) | same; evaluated once by `tools/ml/evaluate_test.py` |
| final product trials | F01, live during `final_test.py` | 20 trials per class | never used for training or tuning |

Hyperparameters are chosen by leave-one-session-out CV on the training sessions only. As planned here, S04 results led to a design change (label consistency, 2026-11-18), so S04 was re-designated validation and a new held-out session (S06) was collected. `train.py` never loads test sessions when validation sessions exist; `tools/ml/freeze.py` hashes the model, thresholds, headers, training code, split and firmware image before S06 is recorded, and `evaluate_test.py` refuses to run if anything changed. After an evaluation, retraining and re-freezing are refused until a new test session exists (or a documented `--reason` is given).

Round 2 recording (`tools/hw/round2.py`): LCD prompts with the action first; every confirmation typed in the terminal (board keys were not trusted while their mapping was unresolved); a handling trial with < 80 % moving 2 s windows is automatically excluded (kept on disk) and repeated.

## Participants and environment variation

The operator ID is anonymous. Record the room, surface and placement in plain words, with no personal data. Using at least two different surfaces/placements across the training sessions is recommended.

## Commands (hardware session 1)

The normal path is one command. It reads `data/collection_plan.json`, shows each session's set-up on the LCD and terminal, waits for UP, runs the trials on beeps, and is resumable:

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $py tools\hw\collect_all.py
```

In board-UI mode (S01–S04) trials were kept without a post-trial key question; from round 2 (S05, S06) every trial is confirmed by typing in the terminal (`--ui terminal`). The trial JSON records `operator_verdict`.

Manual equivalent, one session at a time:

```powershell
& $py tools\hw\capture.py --session S01 --operator P1 --placement bare-board-handheld --surface wood
& $py tools\hw\capture.py --session S02 --operator P1 --placement bare-board-handheld --surface fabric
& $py tools\hw\capture.py --session S03 --operator P2 --placement bare-board-handheld --surface laminate
& $py tools\hw\capture.py --session S04 --operator P1 --placement bare-board-handheld --surface shelf
```
