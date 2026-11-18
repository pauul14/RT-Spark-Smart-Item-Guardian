"""Raw trial loading, cleaning, windowing and leakage-aware session splitting.

On-disk format (written by tools/hw/capture.py):
  <root>/<session>/<trial>.csv   columns: seq,t_us,ax,ay,az,gx,gy,gz,flags
  <root>/<session>/<trial>.json  metadata: label, session, trial, gyro_bias, fs_hz, ...

A trial holds exactly one label. Windows are cut from the middle of the trial
(TRIM_S removed at both ends to drop the operator's start/stop transitions),
are non-overlapping, and never cross a sequence gap. Train/test assignment is
done by SESSION (data/splits.json), so no window of a held-out session, nor a
neighbour of one, can reach training.
"""
import csv
import hashlib
import json
import os
from dataclasses import dataclass, field

from . import LABEL_TO_ID
from . import features as F

TRIM_S = 1.0
MAX_GAP_SAMPLES = 2       # same tolerance as task_infer.c (MAX_GAPS)
FLAG_INVALID = 0x01


@dataclass
class Trial:
    session: str
    trial: str
    label: str
    gyro_bias: tuple
    samples: list            # list of (seq, t_us, ax, ay, az, gx, gy, gz, flags)
    meta: dict
    path: str
    sha256: str


@dataclass
class Window:
    wid: str
    session: str
    trial: str
    label: int
    gyro_bias: tuple
    raw: list                # 200 x (ax, ay, az, gx, gy, gz)
    features: list = field(default_factory=list)


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_trials(root):
    trials = []
    if not os.path.isdir(root):
        return trials
    for session in sorted(os.listdir(root)):
        sdir = os.path.join(root, session)
        if not os.path.isdir(sdir):
            continue
        for name in sorted(os.listdir(sdir)):
            if not name.endswith(".csv"):
                continue
            csv_path = os.path.join(sdir, name)
            meta_path = csv_path[:-4] + ".json"
            if not os.path.isfile(meta_path):
                continue
            with open(meta_path, encoding="utf-8") as fh:
                meta = json.load(fh)
            if meta.get("excluded"):
                continue
            rows = []
            with open(csv_path, newline="", encoding="utf-8") as fh:
                for r in csv.DictReader(fh):
                    rows.append(tuple(int(r[k]) for k in ("seq", "t_us", "ax", "ay", "az", "gx", "gy", "gz", "flags")))
            trials.append(Trial(session=meta["session"], trial=meta["trial"], label=meta["label"],
                                gyro_bias=tuple(float(v) for v in meta["gyro_bias"]), samples=rows,
                                meta=meta, path=csv_path, sha256=_sha256(csv_path)))
    return trials


def window_trial(t, exclusions):
    """Cut non-overlapping, gap-free, valid windows from one trial."""
    if t.label not in LABEL_TO_ID:
        exclusions.append({"trial": f"{t.session}/{t.trial}", "reason": f"unknown label {t.label}"})
        return []
    trim = int(TRIM_S * F.FS_HZ)
    s = t.samples[trim:len(t.samples) - trim]
    out = []
    i = 0
    while i + F.WINDOW_SAMPLES <= len(s):
        chunk = s[i:i + F.WINDOW_SAMPLES]
        gaps = sum(max(0, chunk[k][0] - chunk[k - 1][0] - 1) for k in range(1, len(chunk)))
        invalid = sum(1 for r in chunk if r[8] & FLAG_INVALID)
        wid = f"{t.session}/{t.trial}/w{i // F.WINDOW_SAMPLES}"
        if gaps > MAX_GAP_SAMPLES or invalid:
            exclusions.append({"trial": f"{t.session}/{t.trial}", "window_start_seq": chunk[0][0],
                               "reason": f"gaps={gaps} invalid={invalid}"})
        else:
            out.append(Window(wid=wid, session=t.session, trial=t.trial, label=LABEL_TO_ID[t.label],
                              gyro_bias=t.gyro_bias, raw=[r[2:8] for r in chunk]))
        i += F.WINDOW_SAMPLES
    return out


def build_windows(trials):
    exclusions, windows = [], []
    for t in trials:
        windows.extend(window_trial(t, exclusions))
    for w in windows:
        w.features = F.compute(w.raw, w.gyro_bias)
    return windows, exclusions


MIN_ACTIVE_WINDOWS = 3    # a handling trial with fewer active windows did not perform its label


def label_consistency(train, test, exclusions, thresholds=None):
    """Drop handling-labelled windows in which no handling happened.

    A trial carries one label, but operators paused or stopped moving the box
    inside NORMAL/ROUGH trials (seen in S01-S04: 10-14 s of motion, then rest).
    Those 2 s windows are physically identical to STATIONARY and would teach the
    tree a wrong label. Rule, fixed from TRAINING data only: a handling-labelled
    window is 'active' if gyro_max OR acc_std exceeds the largest value seen in
    any training STATIONARY window; inactive handling windows are excluded (never
    relabelled) and logged with the reason. STATIONARY windows are never touched.
    The same thresholds are applied to the held-out test windows; train.py also
    reports the metrics on the uncleaned test set for transparency.
    thresholds: frozen values (tools/ml/evaluate_test.py) - then nothing is derived here.
    Returns (train, test, info).
    """
    gi, ai = F.FEATURE_NAMES.index("gyro_max"), F.FEATURE_NAMES.index("acc_std")
    still_id = LABEL_TO_ID["STATIONARY"]
    if thresholds:
        thr = dict(thresholds)
    else:
        still = [w for w in train if w.label == still_id]
        if not still:
            return train, test, None
        thr = {"gyro_max_dps": max(w.features[gi] for w in still), "acc_std_g": max(w.features[ai] for w in still)}

    def active(w):
        return w.features[gi] > thr["gyro_max_dps"] or w.features[ai] > thr["acc_std_g"]

    per_trial, kept_tr, kept_te = {}, [], []
    for src, dst in ((train, kept_tr), (test, kept_te)):
        for w in src:
            key = f"{w.session}/{w.trial}"
            pt = per_trial.setdefault(key, {"label": w.label, "windows": 0, "active": 0})
            pt["windows"] += 1
            if w.label == still_id or active(w):
                pt["active"] += 1
                dst.append(w)
            else:
                exclusions.append({"trial": key, "window": w.wid,
                                   "reason": "label_inconsistent: no handling motion in a handling-labelled window "
                                             f"(gyro_max {w.features[gi]:.2f} dps, acc_std {w.features[ai]:.4f} g)"})
    not_performed = sorted(k for k, v in per_trial.items()
                           if v["label"] != still_id and v["active"] < MIN_ACTIVE_WINDOWS)
    info = {"rule": "handling window kept if gyro_max > T_g or acc_std > T_a (T from training STATIONARY maxima)",
            "thresholds": {k: round(v, 5) for k, v in thr.items()},
            "excluded_windows": sum(v["windows"] - v["active"] for v in per_trial.values()),
            "per_trial": per_trial, "trials_label_not_performed": not_performed,
            "min_active_windows": MIN_ACTIVE_WINDOWS}
    return kept_tr, kept_te, info


def load_splits(path):
    with open(path, encoding="utf-8") as fh:
        sp = json.load(fh)
    groups = [set(sp["train_sessions"]), set(sp.get("validation_sessions", [])), set(sp["test_sessions"]),
              set(sp.get("final_trial_sessions", []))]
    overlap = set()
    for i in range(len(groups)):
        for j in range(i + 1, len(groups)):
            overlap |= groups[i] & groups[j]
    if overlap:
        raise ValueError(f"sessions assigned to more than one split: {sorted(overlap)}")
    return sp


def eval_sessions(splits):
    """Sessions train.py evaluates on: the validation sessions when the split defines any
    (then the test sessions are reserved for tools/ml/evaluate_test.py and never read
    during training), otherwise the test sessions (round-1 behaviour)."""
    return list(splits.get("validation_sessions") or splits["test_sessions"])


def split_windows(windows, splits):
    """(train, evaluation, unused) - evaluation = eval_sessions(splits)."""
    train_s, test_s = set(splits["train_sessions"]), set(eval_sessions(splits))
    train = [w for w in windows if w.session in train_s]
    test = [w for w in windows if w.session in test_s]
    unused = sorted({w.session for w in windows} - train_s - test_s - set(splits.get("test_sessions", [])))
    return train, test, unused


def manifest_rows(trials, windows, splits):
    by_trial = {}
    for w in windows:
        by_trial[(w.session, w.trial)] = by_trial.get((w.session, w.trial), 0) + 1
    split_of = {}
    for s in splits.get("train_sessions", []):
        split_of[s] = "train"
    for s in splits.get("validation_sessions", []):
        split_of[s] = "validation"
    for s in splits.get("test_sessions", []):
        split_of[s] = "test"
    for s in splits.get("final_trial_sessions", []):
        split_of[s] = "final"
    rows = []
    for t in trials:
        n = len(t.samples)
        dur = (t.samples[-1][1] - t.samples[0][1]) / 1e6 if n > 1 else 0.0
        rows.append({
            "session": t.session, "trial": t.trial, "label": t.label, "split": split_of.get(t.session, "unassigned"),
            "samples": n, "duration_s": round(dur, 2), "windows": by_trial.get((t.session, t.trial), 0),
            "placement": t.meta.get("placement", ""), "surface": t.meta.get("surface", ""),
            "operator": t.meta.get("operator_id", ""), "synthetic": bool(t.meta.get("synthetic", False)),
            "recorded_utc": t.meta.get("recorded_utc", ""), "file": os.path.relpath(t.path).replace("\\", "/"),
            "sha256": t.sha256,
        })
    return rows
