# Model report `dt-ac8173075c`


Data hash `bf9df326363dabdc` - 24 trials, 264 valid windows, 0 excluded windows.

## Dataset requirement check

- training windows per class {'STILL': 66, 'NORMAL': 66, 'ROUGH': 66} (need >= 30): **met**
- held-out test windows per class {'STILL': 22, 'NORMAL': 22, 'ROUGH': 22} (need >= 10): **met**
- train sessions ['S01', 'S02', 'S03'], test sessions ['S04'], overlap []

## Selected model

max_depth=5, min_samples_leaf=4 -> depth 5, 29 nodes, uses ['acc_mean', 'acc_std', 'acc_min', 'jerk_mean', 'gyro_max']

```
if acc_mean <= 1.00611:
    if acc_mean <= 1.0048:
        if acc_mean <= 1.00467:
            -> NORMAL_HANDLING  counts=[1, 2, 1]
        else:
            -> NORMAL_HANDLING  counts=[0, 6, 0]
    else:
        if jerk_mean <= 0.214538:
            -> ROUGH_HANDLING  counts=[2, 0, 5]
        else:
            if gyro_max <= 2.13791:
                if acc_mean <= 1.00591:
                    -> STATIONARY  counts=[58, 13, 3]
                else:
                    -> STATIONARY  counts=[5, 5, 3]
            else:
                -> NORMAL_HANDLING  counts=[0, 3, 1]
else:
    if acc_mean <= 1.03575:
        if jerk_mean <= 3.2179:
            if acc_mean <= 1.0065:
                if acc_std <= 0.00321794:
                    -> ROUGH_HANDLING  counts=[0, 1, 3]
                else:
                    -> NORMAL_HANDLING  counts=[0, 8, 0]
            else:
                if acc_std <= 0.0456688:
                    -> ROUGH_HANDLING  counts=[0, 0, 11]
                else:
                    -> NORMAL_HANDLING  counts=[0, 3, 3]
        else:
            if acc_mean <= 1.03087:
                -> NORMAL_HANDLING  counts=[0, 19, 0]
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

accuracy 0.439, macro-F1 0.349, n=66

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 0 | 0 | 22 |
| NORMAL | 0 | 8 | 14 |
| ROUGH | 0 | 1 | 21 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 0.000 | 0.000 | 0.000 | 22 |
| NORMAL_HANDLING | 0.889 | 0.364 | 0.516 | 22 |
| ROUGH_HANDLING | 0.368 | 0.955 | 0.532 | 22 |

## Held-out test sessions - rule baseline

thresholds {"still_acc_std": 0.01064, "still_gyro_max": 0.57523, "rough_dev_max": 0.07027, "rough_gyro_max": 74.20902}

accuracy 0.561, macro-F1 0.484, n=66

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 9 | 2 | 11 |
| ROUGH | 7 | 2 | 13 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 0.579 | 1.000 | 0.733 | 22 |
| NORMAL_HANDLING | 0.500 | 0.091 | 0.154 | 22 |
| ROUGH_HANDLING | 0.542 | 0.591 | 0.565 | 22 |

## Training sessions (for reference, optimistic)

tree accuracy 0.813 / macro-F1 0.812; baseline accuracy 0.545 / macro-F1 0.489

## Misclassified held-out windows (37)

- S04/NORMAL_HANDLING_01/w4: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_01/w6: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_01/w7: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_01/w8: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_01/w9: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_01/w10: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_02/w0: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_02/w1: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_02/w2: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_02/w3: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w6: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline ROUGH_HANDLING
- S04/NORMAL_HANDLING_02/w8: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline STATIONARY
- S04/NORMAL_HANDLING_02/w9: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline NORMAL_HANDLING
- S04/NORMAL_HANDLING_02/w10: true NORMAL_HANDLING, tree ROUGH_HANDLING, baseline NORMAL_HANDLING
- S04/ROUGH_HANDLING_02/w6: true ROUGH_HANDLING, tree NORMAL_HANDLING, baseline ROUGH_HANDLING
- S04/STATIONARY_01/w0: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w1: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w2: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w3: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w4: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w5: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w6: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w7: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w8: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w9: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_01/w10: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w0: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w1: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w2: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w3: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w4: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w5: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w6: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w7: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w8: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w9: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
- S04/STATIONARY_02/w10: true STATIONARY, tree ROUGH_HANDLING, baseline STATIONARY
