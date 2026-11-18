"""Build the PLACEHOLDER model used before real board data exists.

Generates synthetic trials into data/synthetic/ (never data/raw/), trains with
data/splits_synthetic.json and exports src/model/*.h with
SIG_MODEL_IS_PLACEHOLDER = 1. This only proves the pipeline end to end
(training -> export -> firmware build -> host C parity); it is not a result.
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from sigml import synth  # noqa: E402

syn_dir = os.path.join(ROOT, "data", "synthetic")
synth.generate(syn_dir)
splits = {"_comment": "SYNTHETIC placeholder split - not board data",
          "train_sessions": ["SYN01", "SYN02", "SYN03"], "test_sessions": ["SYN04"], "final_trial_sessions": []}
sp_path = os.path.join(ROOT, "data", "splits_synthetic.json")
with open(sp_path, "w") as fh:
    json.dump(splits, fh, indent=1)
sys.exit(subprocess.call([sys.executable, os.path.join(HERE, "train.py"), "--data", syn_dir, "--splits", sp_path,
                          "--placeholder"]))
