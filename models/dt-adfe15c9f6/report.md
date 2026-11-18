# Model report `dt-adfe15c9f6`


Data hash `b5ab1c8211cc11b4` - 28 trials, 308 valid windows, 72 excluded windows.

## Dataset requirement check

- training windows per class {'STILL': 66, 'NORMAL': 77, 'ROUGH': 46} (need >= 30): **met**
- validation windows per class {'STILL': 22, 'NORMAL': 11, 'ROUGH': 14} (need >= 10): **met**
- train sessions ['S01', 'S02', 'S03', 'S05'], validation sessions ['S04'], overlap []; test sessions ['S06'] reserved for tools/ml/evaluate_test.py (not loaded here)

## Label consistency (handling windows without handling motion)

Rule: handling window kept if gyro_max > T_g or acc_std > T_a (T from training STATIONARY maxima); thresholds from training STATIONARY windows {'gyro_max_dps': 2.11005, 'acc_std_g': 0.01149}. Excluded 72 handling-labelled windows (never relabelled). Per trial, kept/total:

| trial | label | kept / total |
|---|---|---|
| S01/NORMAL_HANDLING_01 | NORMAL_HANDLING | 6 / 11 |
| S01/NORMAL_HANDLING_02 | NORMAL_HANDLING | 7 / 11 |
| S01/ROUGH_HANDLING_05 | ROUGH_HANDLING | 6 / 11 |
| S01/ROUGH_HANDLING_06 | ROUGH_HANDLING | 10 / 11 |
| S02/NORMAL_HANDLING_02 | NORMAL_HANDLING | 7 / 11 |
| S02/NORMAL_HANDLING_03 | NORMAL_HANDLING | 6 / 11 |
| S02/ROUGH_HANDLING_01 | ROUGH_HANDLING | 10 / 11 |
| S02/ROUGH_HANDLING_02 | ROUGH_HANDLING | 7 / 11 |
| S03/NORMAL_HANDLING_01 | NORMAL_HANDLING | 6 / 11 |
| S03/NORMAL_HANDLING_02 | NORMAL_HANDLING | 1 / 11 |
| S03/ROUGH_HANDLING_01 | ROUGH_HANDLING | 8 / 11 |
| S03/ROUGH_HANDLING_02 | ROUGH_HANDLING | 5 / 11 |
| S04/NORMAL_HANDLING_01 | NORMAL_HANDLING | 6 / 11 |
| S04/NORMAL_HANDLING_02 | NORMAL_HANDLING | 5 / 11 |
| S04/ROUGH_HANDLING_01 | ROUGH_HANDLING | 7 / 11 |
| S04/ROUGH_HANDLING_02 | ROUGH_HANDLING | 7 / 11 |

Trials whose labelled activity was essentially not performed (< 3 active windows): S03/NORMAL_HANDLING_02

## Selected model

max_depth=2, min_samples_leaf=8 -> depth 2, 5 nodes, uses ['acc_mean', 'gyro_max']

```
if gyro_max <= 2.13791:
    -> STATIONARY  counts=[66, 0, 0]
else:
    if acc_mean <= 1.04237:
        -> NORMAL_HANDLING  counts=[0, 75, 13]
    else:
        -> ROUGH_HANDLING  counts=[0, 2, 33]
```

## Validation sessions ['S04'] - decision tree

accuracy 0.851, macro-F1 0.813, n=47

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 9 | 2 |
| ROUGH | 0 | 5 | 9 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 0.643 | 0.818 | 0.720 | 11 |
| ROUGH_HANDLING | 0.818 | 0.643 | 0.720 | 14 |

## Validation sessions - rule baseline

thresholds {"still_acc_std": 0.03016, "still_gyro_max": 22.26005, "rough_dev_max": 0.3521, "rough_gyro_max": 499.82996}

accuracy 0.723, macro-F1 0.577, n=47

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 1 | 10 |
| ROUGH | 3 | 0 | 11 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 0.880 | 1.000 | 0.936 | 22 |
| NORMAL_HANDLING | 1.000 | 0.091 | 0.167 | 11 |
| ROUGH_HANDLING | 0.524 | 0.786 | 0.629 | 14 |

## Validation sessions - as collected (no label-consistency exclusion)

Every validation window with its trial label, including windows in which the operator had stopped handling the box (those are expected to be predicted STATIONARY).

tree accuracy 0.606, macro-F1 0.581, n=66; baseline accuracy 0.515, macro-F1 0.422

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 11 | 9 | 2 |
| ROUGH | 8 | 5 | 9 |

## Training sessions (for reference, optimistic)

tree accuracy 0.921 / macro-F1 0.908; baseline accuracy 0.693 / macro-F1 0.680

## Misclassified validation windows (7)

- S04/NORMAL_HANDLING_02/w3: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w6: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/ROUGH_HANDLING_01/w2: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline STATIONARY
- S04/ROUGH_HANDLING_01/w3: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline ROUGH_HANDLING
- S04/ROUGH_HANDLING_01/w7: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline STATIONARY
- S04/ROUGH_HANDLING_02/w0: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline STATIONARY
- S04/ROUGH_HANDLING_02/w6: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline ROUGH_HANDLING
