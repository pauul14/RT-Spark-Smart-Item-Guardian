"""After hardware session 1: real data -> trained model -> verified firmware, in one run.

  python tools/ml/post_capture.py            (no hardware is touched)

Steps (stops at the first failure, with the reason):
  1. dataset gate: every session named in data/splits.json exists under data/raw with a
     session.json; no session is in two splits; no synthetic/simulated trial is present;
     kept windows per class >= 30 (train) and >= 10 (held-out test)
  2. tools/ml/train.py on data/raw (refuses to continue if the model is a placeholder)
  3. tools/ml/verify_c_model.py (firmware C == Python, bit-exact, on every held-out window)
  4. tools/run_offline_checks.py (unit tests, parity, firmware build with 0 warnings, footprint, hygiene)
  5. evidence/offline/post_capture_<UTC>.md + .json: model id, held-out metrics vs baseline,
     data counts, and whether gate 2 (trained-model flash + final_test.py) is ready
Final product trials (F01) are recorded live by final_test.py and are never read here.
"""
import datetime
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from sigml import CLASSES, dataset                        # noqa: E402

PY = sys.executable
REQ_TRAIN, REQ_TEST = 30, 10


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def dataset_gate(raw, splits_path):
    problems, info = [], {}
    splits = dataset.load_splits(splits_path)            # raises on overlapping sessions
    # with validation sessions, the test sessions are recorded AFTER the freeze and are not needed here
    keys = ("train_sessions", "validation_sessions") if splits.get("validation_sessions") else \
        ("train_sessions", "test_sessions")
    for key in keys:
        for s in splits[key]:
            sdir = os.path.join(raw, s)
            if not os.path.isfile(os.path.join(sdir, "session.json")):
                problems.append(f"session {s} ({key}) missing or unfinished: {sdir}\\session.json")
    trials = dataset.load_trials(raw)
    bad = [f"{t.session}/{t.trial}" for t in trials if t.meta.get("synthetic") or t.meta.get("simulated")]
    if bad:
        problems.append(f"synthetic/simulated trials under data/raw: {bad[:5]}")
    windows, exclusions = dataset.build_windows(trials)
    train, test, unused = dataset.split_windows(windows, splits)
    with open(os.path.join(ROOT, "tools", "ml", "training_config.json")) as fh:
        cfg = json.load(fh)
    if cfg.get("label_consistency"):                       # same rule as train.py, so the counts match
        train, test, lc = dataset.label_consistency(train, test, exclusions)
        if lc:
            info["label_consistency"] = {k: lc[k] for k in ("thresholds", "excluded_windows",
                                                            "trials_label_not_performed")}
    cnt = lambda ws: [sum(1 for w in ws if w.label == c) for c in range(len(CLASSES))]   # noqa: E731
    info.update({"trials": len(trials), "windows": len(windows), "excluded_windows": len(exclusions),
                 "train_per_class": cnt(train), "test_per_class": cnt(test), "unused_sessions": unused})
    if any(c < REQ_TRAIN for c in info["train_per_class"]):
        problems.append(f"training windows per class {info['train_per_class']} < {REQ_TRAIN}: record more trials "
                        "in a NEW training session (e.g. S05) and add it to train_sessions")
    if any(c < REQ_TEST for c in info["test_per_class"]):
        problems.append(f"held-out windows per class {info['test_per_class']} < {REQ_TEST}: record another "
                        "held-out session and add it to test_sessions")
    return problems, info


def main():
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    raw = os.path.join(ROOT, "data", "raw")
    out = {"utc": stamp, "steps": []}

    def step(name, ok, detail):
        out["steps"].append({"step": name, "ok": ok, "detail": detail})
        print(f"[{'OK' if ok else 'STOP'}] {name}: {detail}")
        return ok

    def finish(ready, rc=None):
        out["gate2_ready"] = ready
        os.makedirs(os.path.join(ROOT, "evidence", "offline"), exist_ok=True)
        base = os.path.join(ROOT, "evidence", "offline", f"post_capture_{stamp}")
        with open(base + ".json", "w") as fh:
            json.dump(out, fh, indent=1)
        md = [f"# Post-capture pipeline {stamp}\n", f"Gate 2 ready: **{'YES' if ready else 'NO'}**\n",
              "| step | result | detail |", "|---|---|---|"]
        md += [f"| {s['step']} | {'OK' if s['ok'] else 'STOP'} | {str(s['detail'])[:300].replace('|', '/')} |"
               for s in out["steps"]]
        with open(base + ".md", "w", encoding="utf-8") as fh:
            fh.write("\n".join(md) + "\n")
        print(f"written: {os.path.relpath(base, ROOT)}.md")
        sys.exit(rc if rc is not None else (0 if ready else 1))

    sys.path.insert(0, HERE)
    import freeze as FZ                                           # noqa: E402
    with open(os.path.join(ROOT, "data", "splits.json")) as fh:
        tests = json.load(fh).get("test_sessions", [])
    done = FZ.evaluations_for(tests)
    if done:
        step("guard", False, f"test sessions {tests} were already evaluated against a frozen model ({done}); "
                             "retraining now would tune against them. Record a NEW test session first.")
        finish(False)
    try:
        problems, info = dataset_gate(raw, os.path.join(ROOT, "data", "splits.json"))
    except ValueError as e:
        step("dataset gate", False, str(e))
        finish(False)
    out["data"] = info
    if not step("dataset gate", not problems, "; ".join(problems) or
                f"train/class {info['train_per_class']}, test/class {info['test_per_class']}, "
                f"{info['excluded_windows']} excluded windows"):
        finish(False)

    rc, log = run([PY, os.path.join("tools", "ml", "train.py")])
    with open(os.path.join(ROOT, "models", "latest.json")) as fh:
        latest = json.load(fh)
    if not step("train", rc == 0 and not latest["placeholder"], log.strip().splitlines()[-3:] if log else rc):
        finish(False)
    with open(os.path.join(ROOT, latest["path"], "report.json")) as fh:
        rep = json.load(fh)
    out["model"] = {"id": latest["model_id"], "test_tree": {k: rep["test"]["tree"][k] for k in ("accuracy", "macro_f1", "confusion")},
                    "test_baseline": {k: rep["test"]["baseline"][k] for k in ("accuracy", "macro_f1", "confusion")},
                    "requirements": {k: rep["requirements"][k] for k in ("train_ok", "test_ok")}}
    if not step("held-out requirements", rep["requirements"]["train_ok"] and rep["requirements"]["test_ok"],
                f"tree acc {rep['test']['tree']['accuracy']:.3f} macroF1 {rep['test']['tree']['macro_f1']:.3f}; "
                f"baseline acc {rep['test']['baseline']['accuracy']:.3f} macroF1 {rep['test']['baseline']['macro_f1']:.3f}"):
        finish(False)
    tf, bf = rep["test"]["tree"]["macro_f1"], rep["test"]["baseline"]["macro_f1"]
    unc = rep.get("test_uncleaned", {}).get("tree", {})
    if not step("model quality (held-out tree >= rule baseline)", tf >= bf,
                f"tree macroF1 {tf:.3f} vs baseline {bf:.3f}"
                + (f"; as-collected tree acc {unc['accuracy']:.3f} macroF1 {unc['macro_f1']:.3f}" if unc else "")
                + ("" if tf >= bf else " - the trained tree does not beat the baseline: do not flash it; "
                   "inspect models/<id>/report.md (labels, per-trial motion) first")):
        finish(False)

    rc, log = run([PY, os.path.join("tools", "ml", "verify_c_model.py")])
    if not step("C parity (host)", rc == 0, log.strip()[-400:]):
        finish(False)
    rc, log = run([PY, os.path.join("tools", "run_offline_checks.py")])
    if not step("offline checks + firmware build", rc == 0, "see evidence/offline/LATEST.md"):
        finish(False)
    with open(os.path.join(ROOT, "data", "splits.json")) as fh:
        sp = json.load(fh)
    if sp.get("validation_sessions"):
        rc, log = run([PY, os.path.join("tools", "ml", "freeze.py")])
        if not step("freeze (model, thresholds, headers, firmware image)", rc == 0, log.strip()[-300:]):
            finish(False)
        step("next", True, f"record the NEW test session(s) {sp['test_sessions']} with the frozen firmware, then "
                           "tools/ml/evaluate_test.py; gate 2 only after that evaluation")
        out["frozen"] = True
        finish(False, rc=0)  # pipeline succeeded; gate 2 is decided by evaluate_test.py, not here
    step("gate 2", True, f"flash model {latest['model_id']} (approval required), then run tools/hw/final_test.py")
    finish(True)


if __name__ == "__main__":
    main()
