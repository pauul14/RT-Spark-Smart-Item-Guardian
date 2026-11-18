# Model report `ph-fd5d5654a0`

> **PLACEHOLDER MODEL - trained on SYNTHETIC data for offline plumbing only. These numbers are not results and must not be reported as such.**


Data hash `b215d51b4016adec` - 24 trials, 264 valid windows, 0 excluded windows.

## Dataset requirement check

- training windows per class {'STILL': 66, 'NORMAL': 66, 'ROUGH': 66} (need >= 30): **met**
- held-out test windows per class {'STILL': 22, 'NORMAL': 22, 'ROUGH': 22} (need >= 10): **met**
- train sessions ['SYN01', 'SYN02', 'SYN03'], test sessions ['SYN04'], overlap []

## Selected model

max_depth=2, min_samples_leaf=8 -> depth 2, 5 nodes, uses ['acc_mean', 'acc_std']

```
if acc_mean <= 1.11654:
    if acc_std <= 0.0382055:
        -> STATIONARY  counts=[66, 0, 0]
    else:
        -> NORMAL_HANDLING  counts=[0, 66, 0]
else:
    -> ROUGH_HANDLING  counts=[0, 0, 66]
```

## Held-out test sessions - decision tree

accuracy 1.000, macro-F1 1.000, n=66

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 22 | 0 |
| ROUGH | 0 | 0 | 22 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 1.000 | 1.000 | 1.000 | 22 |
| ROUGH_HANDLING | 1.000 | 1.000 | 1.000 | 22 |

## Held-out test sessions - rule baseline

thresholds {"still_acc_std": 0.07431, "still_gyro_max": 39.01411, "rough_dev_max": 1.6047, "rough_gyro_max": 331.45187}

accuracy 1.000, macro-F1 1.000, n=66

| true \ predicted | STILL | NORMAL | ROUGH |
|---|---|---|---|
| STILL | 22 | 0 | 0 |
| NORMAL | 0 | 22 | 0 |
| ROUGH | 0 | 0 | 22 |

| class | precision | recall | F1 | support |
|---|---|---|---|---|
| STATIONARY | 1.000 | 1.000 | 1.000 | 22 |
| NORMAL_HANDLING | 1.000 | 1.000 | 1.000 | 22 |
| ROUGH_HANDLING | 1.000 | 1.000 | 1.000 | 22 |

## Training sessions (for reference, optimistic)

tree accuracy 1.000 / macro-F1 1.000; baseline accuracy 0.990 / macro-F1 0.990

## Misclassified held-out windows (0)

