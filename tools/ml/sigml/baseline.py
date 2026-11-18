"""Rule-based baseline (mirrors sig_baseline_predict in src/core/model_infer.c).

    if acc_std <= T_STILL_STD and gyro_max <= T_STILL_GYRO:   STATIONARY
    elif acc_dev_max > T_ROUGH_DEV or gyro_max > T_ROUGH_GYRO: ROUGH_HANDLING
    else:                                                      NORMAL_HANDLING

The four thresholds are chosen by grid search over training-set quantiles,
maximising training macro-F1. The held-out sessions are never used here.
"""
import itertools
from .features import FEATURE_NAMES, f32
from .metrics import summary

I_ACC_STD = FEATURE_NAMES.index("acc_std")
I_DEV_MAX = FEATURE_NAMES.index("acc_dev_max")
I_GYRO_MAX = FEATURE_NAMES.index("gyro_max")


def predict_one(x, p):
    if x[I_ACC_STD] <= p["still_acc_std"] and x[I_GYRO_MAX] <= p["still_gyro_max"]:
        return 0
    if x[I_DEV_MAX] > p["rough_dev_max"] or x[I_GYRO_MAX] > p["rough_gyro_max"]:
        return 2
    return 1


def predict(X, p):
    return [predict_one(x, p) for x in X]


def _quantiles(values, qs):
    v = sorted(values)
    return sorted({f32(v[min(len(v) - 1, int(q * (len(v) - 1)))]) for q in qs})


def fit(X, y):
    qs = [i / 10 for i in range(11)]          # deciles: 11^4 combinations at most
    cand_std = _quantiles([x[I_ACC_STD] for x in X], qs)
    cand_gs = _quantiles([x[I_GYRO_MAX] for x in X], qs)
    cand_dev = _quantiles([x[I_DEV_MAX] for x in X], qs)
    best = None
    for a, b, c, d in itertools.product(cand_std, cand_gs, cand_dev, cand_gs):
        if d < b:
            continue
        p = {"still_acc_std": a, "still_gyro_max": b, "rough_dev_max": c, "rough_gyro_max": d}
        s = summary(y, predict(X, p))
        key = (s["macro_f1"], s["accuracy"])
        if best is None or key > best[0]:
            best = (key, p)
    return best[1]
