"""sigml - Smart Item Guardian model development (Python 3.11 standard library only).

Modules
-------
features  float32-exact mirror of src/core/features.c
dataset   raw trial loading, cleaning, windowing, session split
cart      bounded CART decision tree (gini)
baseline  rule-based baseline with thresholds tuned on training sessions
metrics   confusion matrix, per-class precision/recall/F1
export    C headers for the firmware (tree, config, baseline, golden vectors)
synth     clearly-labelled SYNTHETIC data for offline plumbing only
"""

CLASSES = ("STATIONARY", "NORMAL_HANDLING", "ROUGH_HANDLING")
CLASS_SHORT = ("STILL", "NORMAL", "ROUGH")
LABEL_TO_ID = {name: i for i, name in enumerate(CLASSES)}
