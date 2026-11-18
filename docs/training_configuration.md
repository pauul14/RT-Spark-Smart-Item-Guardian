# Training configuration

| item | value |
|---|---|
| environment | PlatformIO's Python 3.11.7, standard library only (no numpy/scikit-learn), deterministic |
| entry point | `tools/ml/train.py --data data/raw --splits data/splits.json` |
| features (12, binary32) | acc_mean, acc_std, acc_min, acc_max, acc_dev_max, jerk_mean, jerk_max, gyro_mean, gyro_std, gyro_max, high_g_frac (\|a\|−1 g > 0.40 g), axis_std_max |
| C implementation | `src/core/features.c`; the Python mirror `tools/ml/sigml/features.py` rounds every operation to binary32 in the same order |
| model | CART decision tree, Gini, thresholds are binary32 midpoints, `x <= t` goes left |
| grid | `tools/ml/training_config.json`: max_depth ∈ {2,3,4,5}, min_samples_leaf ∈ {2,4,8} |
| selection | leave-one-session-out CV on training sessions, mean macro-F1; ties → shallower tree |
| baseline | 4-threshold rule (acc_std, gyro_max for STILL; acc_dev_max, gyro_max for ROUGH), decile grid search on training windows, macro-F1 |
| evaluation | held-out test sessions only: confusion matrix, per-class precision/recall/F1, accuracy, macro-F1, misclassified window list |
| export | `src/model/model_tree.h`, `model_config.h`, `baseline_params.h`, `golden_vectors.h` (≤ 30 class-balanced held-out windows + Python features/predictions) |
| artifacts | `models/<model_id>/`: model.json, report.md/json, test_predictions.csv, heldout_windows.jsonl, data_manifest.csv; `models/latest.json` |
| model id | `dt-<sha256(tree + data hash)[:10]>`; placeholder models use `ph-` |
| verification | `tools/ml/verify_c_model.py`: host-compiled firmware C vs Python on every held-out window (bit-exact features + class + baseline); `golden` on target |

## Current state

- `models/ph-fd5d5654a0` is a **placeholder**: depth 2, 5 nodes, trained on SYNTHETIC data. It exists only to prove the pipeline: training → export → firmware build → host parity → on-target golden test. Its accuracy numbers are meaningless and are not reported as results.
- The trained model is **`dt-adfe15c9f6`** (depth 2, 5 nodes, 55 B of tables): trained on S01–S03 + S05, validated on S04, frozen, then evaluated once on the independent session S06 (accuracy / macro-F1 0.970 vs rule baseline 0.924). Report: `models/dt-adfe15c9f6/report.md`, `evidence/offline/test_eval_S06_20261118T065156Z.md`. All recordings use the bare board held by hand.

## After session 1 (no hardware needed)

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $py tools\ml\train.py                        # real data, data/splits.json
& $py tools\run_offline_checks.py              # unit tests, C parity, build, footprint
```
