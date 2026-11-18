# Models

| id | data | status |
|---|---|---|
| `ph-fd5d5654a0` | SYNTHETIC trials (`tools/ml/make_placeholder.py`) | **Placeholder**, used only to test training → export → build → parity. Firmware built with it shows "PLACEHOLDER MODEL" and reports `placeholder=1`. Its metrics are not results. |
| `dt-…` | real board data, S01–S03 train / S04 test | **pending** hardware session 1 |

`latest.json` points to the model whose headers are currently in `src/model/`; `tools/run_offline_checks.py` fails if they disagree. Each model folder contains:

| file | content |
|---|---|
| `model.json` | tree, features, preprocessing, hyperparameters, baseline thresholds, data hash |
| `report.md` / `report.json` | held-out metrics, confusion matrices, CV table, exclusions, misclassified windows |
| `test_predictions.csv` | per-window labels, tree and baseline predictions, and features |
| `heldout_windows.jsonl` | raw windows for the host C parity check |
| `data_manifest.csv` | trials with SHA-256 |
