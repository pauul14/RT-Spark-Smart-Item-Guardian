# Independent test evaluation - S06 - model `dt-adfe15c9f6`

Model frozen 2026-11-18T06:29:40Z (models/frozen.json); freeze intact before and after this run: **yes**. Test sessions present at freeze time: none. Nothing was trained or tuned on S06.

## Data

- trials used: 6; excluded trials (kept on disk): none
- windows as collected {'STILL': 22, 'NORMAL': 22, 'ROUGH': 22}; after the frozen label-consistency rule {'STILL': 22, 'NORMAL': 22, 'ROUGH': 22}; windows excluded for sample quality: 0
- >= 10 valid filtered windows per class: **met**
- frozen thresholds {'gyro_max_dps': 2.11005, 'acc_std_g': 0.01149}; handling windows without motion excluded: 0; per trial kept/total: S06/NORMAL_HANDLING_01 11/11, S06/NORMAL_HANDLING_02 11/11, S06/ROUGH_HANDLING_01 11/11, S06/ROUGH_HANDLING_02 11/11

## Filtered (frozen label-consistency rule)

| | decision tree | rule baseline |
|---|---|---|
| accuracy | 0.970 | 0.924 |
| macro-F1 | 0.970 | 0.924 |
| n | 66 | 66 |

Decision tree:

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 22 | 0 |
| ROUGH | 0 | 2 | 20 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 0.917 | 1.000 | 0.957 | 22 |
| ROUGH_HANDLING | 1.000 | 0.909 | 0.952 | 22 |

Rule baseline:

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 19 | 3 |
| ROUGH | 0 | 2 | 20 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 0.905 | 0.864 | 0.884 | 22 |
| ROUGH_HANDLING | 0.870 | 0.909 | 0.889 | 22 |


## As collected

| | decision tree | rule baseline |
|---|---|---|
| accuracy | 0.970 | 0.924 |
| macro-F1 | 0.970 | 0.924 |
| n | 66 | 66 |

Decision tree:

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 22 | 0 |
| ROUGH | 0 | 2 | 20 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 0.917 | 1.000 | 0.957 | 22 |
| ROUGH_HANDLING | 1.000 | 0.909 | 0.952 | 22 |

Rule baseline:

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 19 | 3 |
| ROUGH | 0 | 2 | 20 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 0.905 | 0.864 | 0.884 | 22 |
| ROUGH_HANDLING | 0.870 | 0.909 | 0.889 | 22 |


## Misclassified filtered windows (2)

- S06/ROUGH_HANDLING_01/w9: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline NORMAL_HANDLING
- S06/ROUGH_HANDLING_01/w10: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline NORMAL_HANDLING

## C/Python parity on S06

66/66 classes, features bit-exact 66/66, baseline 66/66: **PASS**

## Gate 2 model readiness: **YES** (>= 10 windows/class, parity PASS, freeze intact, tree macro-F1 >= baseline on the filtered set)

