# Model report `dt-d8b2af7075`


Data hash `bf9df326363dabdc` - 24 trials, 264 valid windows, 72 excluded windows.

## Dataset requirement check

- training windows per class {'STILL': 66, 'NORMAL': 33, 'ROUGH': 46} (need >= 30): **met**
- held-out test windows per class {'STILL': 22, 'NORMAL': 11, 'ROUGH': 14} (need >= 10): **met**
- train sessions ['S01', 'S02', 'S03'], test sessions ['S04'], overlap []

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

max_depth=4, min_samples_leaf=4 -> depth 4, 15 nodes, uses ['acc_mean', 'acc_min', 'jerk_mean', 'gyro_max']

```
if gyro_max <= 2.13791:
    -> STATIONARY  counts=[66, 0, 0]
else:
    if acc_mean <= 1.03575:
        if jerk_mean <= 3.2179:
            if acc_mean <= 1.0057:
                -> NORMAL_HANDLING  counts=[0, 3, 1]
            else:
                -> ROUGH_HANDLING  counts=[0, 3, 9]
        else:
            if acc_mean <= 1.03087:
                -> NORMAL_HANDLING  counts=[0, 21, 0]
            else:
                -> NORMAL_HANDLING  counts=[0, 3, 1]
    else:
        if acc_min <= 0.254759:
            -> NORMAL_HANDLING  counts=[0, 2, 2]
        else:
            if jerk_mean <= 7.65627:
                -> ROUGH_HANDLING  counts=[0, 0, 30]
            else:
                -> ROUGH_HANDLING  counts=[0, 1, 3]
```

## Held-out test sessions - decision tree

accuracy 0.894, macro-F1 0.851, n=47

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 6 | 5 |
| ROUGH | 0 | 0 | 14 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 1.000 | 0.545 | 0.706 | 11 |
| ROUGH_HANDLING | 0.737 | 1.000 | 0.848 | 14 |

## Held-out test sessions - rule baseline

thresholds {"still_acc_std": 0.02887, "still_gyro_max": 37.96953, "rough_dev_max": 0.3521, "rough_gyro_max": 499.82996}

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

## Held-out test sessions - as collected (no label-consistency exclusion)

Every held-out window with its trial label, including windows in which the operator had stopped handling the box (those are expected to be predicted STATIONARY).

tree accuracy 0.636, macro-F1 0.603, n=66; baseline accuracy 0.515, macro-F1 0.422

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 11 | 6 | 5 |
| ROUGH | 8 | 0 | 14 |

## Training sessions (for reference, optimistic)

tree accuracy 0.945 / macro-F1 0.931; baseline accuracy 0.738 / macro-F1 0.634

## Misclassified held-out windows (5)

- S04/NORMAL_HANDLING_01/w0: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline NORMAL_HANDLING
- S04/NORMAL_HANDLING_01/w4: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w3: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w6: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w7: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
