"""Freeze / verify the trained model before an independent test session is evaluated.

  python tools/ml/freeze.py            write models/frozen.json for models/latest.json
  python tools/ml/freeze.py --verify   exit 0 only if nothing listed in frozen.json changed

frozen.json records SHA-256 hashes of everything that defines the predictions:
model.json (tree, baseline thresholds, label-consistency thresholds, hyperparameters),
the exported firmware headers (src/model/*.h), the training code and configuration,
the split file and the built firmware image. tools/ml/evaluate_test.py refuses to
run when any of them differs, so the test session cannot influence the model.
Re-freezing after a test evaluation exists for the same test sessions is refused
unless --reason is given; the reason is recorded in frozen.json and its history.
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FROZEN = os.path.join(ROOT, "models", "frozen.json")
FW_BIN = os.path.join(ROOT, ".pio", "build", "rt_spark", "firmware.bin")


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def tracked_files(model_dir):
    files = [os.path.join(model_dir, "model.json")]
    files += sorted(glob.glob(os.path.join(ROOT, "src", "model", "*.h")))
    files += sorted(glob.glob(os.path.join(ROOT, "tools", "ml", "sigml", "*.py")))
    files += [os.path.join(ROOT, "tools", "ml", n) for n in ("train.py", "training_config.json")]
    files += [os.path.join(ROOT, "data", "splits.json"), os.path.join(ROOT, "src", "core", "features.c"),
              os.path.join(ROOT, "src", "core", "model_infer.c")]
    if os.path.isfile(FW_BIN):
        files.append(FW_BIN)
    return files


def evaluations_for(test_sessions):
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "evidence", "offline", "test_eval_*.json"))):
        with open(p) as fh:
            d = json.load(fh)
        if set(d.get("test_sessions", [])) & set(test_sessions):
            out.append(rel(p))
    return out


def build_stamps():
    """__DATE__ __TIME__ strings compiled into firmware.bin (what '#VER,...built=' reports)."""
    if not os.path.isfile(FW_BIN):
        return []
    import re
    data = open(FW_BIN, "rb").read()
    # __DATE__ and __TIME__ are separate string literals in the image; the board prints "<date> <time>"
    dates = {m.decode() for m in re.findall(rb"[A-Z][a-z]{2} [ 0-9]\d \d{4}(?=\x00)", data)}
    times = {m.decode() for m in re.findall(rb"(?<=\x00)\d\d:\d\d:\d\d(?=\x00)", data)}
    return sorted(f"{d} {t}" for d in dates for t in times)


ML_EXEMPT = {".pio/build/rt_spark/firmware.bin"}     # integration changes may only change the firmware image


def successor(reason):
    """Documented successor freeze for a firmware integration change: the previous record is
    archived unchanged, every ML artifact must hash identically, only the firmware image may differ."""
    if not reason:
        sys.exit("--successor needs --reason")
    if not os.path.isfile(FROZEN):
        sys.exit("no previous freeze to succeed")
    with open(FROZEN) as fh:
        old = json.load(fh)
    hist_dir = os.path.join(ROOT, "models", "freeze_history")
    os.makedirs(hist_dir, exist_ok=True)
    arch = os.path.join(hist_dir, f"frozen_{old['model_id']}_{old['frozen_utc'].replace(':', '').replace('-', '')}.json")
    if not os.path.isfile(arch):
        with open(arch, "w") as fh:
            json.dump(old, fh, indent=1)
    mdir = os.path.join(ROOT, old["model_dir"])
    new_h = {rel(p): sha(p) for p in tracked_files(mdir)}
    ml_changed = [p for p, h in old["hashes"].items() if p not in ML_EXEMPT and new_h.get(p) != h]
    if ml_changed:
        sys.exit("refusing successor freeze: ML artifacts differ from the frozen model: " + "; ".join(ml_changed))
    changed = sorted(p for p in new_h if old["hashes"].get(p) != new_h[p])
    out = dict(old)
    out.update({
        "frozen_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reason": reason, "kind": "successor (firmware integration change; model and ML artifacts unchanged)",
        "successor_of": rel(arch), "ml_artifacts_unchanged": True, "changed_files": changed,
        "hashes": new_h, "firmware_bin_included": os.path.isfile(FW_BIN), "firmware_built_stamps": build_stamps(),
        "history": old.get("history", []) + [{k: old[k] for k in ("model_id", "frozen_utc", "reason") if k in old}
                                             | {"archived_as": rel(arch)}],
    })
    with open(FROZEN, "w") as fh:
        json.dump(out, fh, indent=1)
    return out


def freeze(reason=None):
    with open(os.path.join(ROOT, "models", "latest.json")) as fh:
        latest = json.load(fh)
    if latest["placeholder"]:
        sys.exit("refusing to freeze a placeholder model")
    with open(os.path.join(ROOT, "data", "splits.json")) as fh:
        splits = json.load(fh)
    tests = splits.get("test_sessions", [])
    done = evaluations_for(tests)
    if done and not reason:
        sys.exit(f"test sessions {tests} were already evaluated ({done}); re-freezing would tune against "
                 "them. Record a NEW test session, or pass --reason to document why.")
    mdir = os.path.join(ROOT, latest["path"])
    with open(os.path.join(mdir, "model.json")) as fh:
        model = json.load(fh)
    history = []
    if os.path.isfile(FROZEN):
        with open(FROZEN) as fh:
            old = json.load(fh)
        history = old.get("history", []) + [{k: old[k] for k in ("model_id", "frozen_utc", "reason") if k in old}]
    present = sorted(d for d in os.listdir(os.path.join(ROOT, "data", "raw"))
                     if os.path.isdir(os.path.join(ROOT, "data", "raw", d)))
    out = {
        "model_id": model["model_id"], "model_dir": latest["path"],
        "frozen_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reason": reason or "initial freeze before the independent test session",
        "hyperparameters": model["hyperparameters"], "label_consistency": model.get("label_consistency"),
        "baseline": model["baseline"], "preprocessing": model["preprocessing"],
        "train_sessions": splits["train_sessions"], "validation_sessions": splits.get("validation_sessions", []),
        "test_sessions": tests,
        "test_sessions_present_at_freeze": [s for s in tests if s in present],
        "firmware_bin_included": os.path.isfile(FW_BIN), "firmware_built_stamps": build_stamps(),
        "hashes": {rel(p): sha(p) for p in tracked_files(mdir)},
        "history": history,
    }
    with open(FROZEN, "w") as fh:
        json.dump(out, fh, indent=1)
    return out


def verify():
    """Returns (ok, frozen, changed)."""
    if not os.path.isfile(FROZEN):
        return False, None, ["models/frozen.json missing - run tools/ml/freeze.py first"]
    with open(FROZEN) as fh:
        fr = json.load(fh)
    changed = []
    for p, h in fr["hashes"].items():
        ap = os.path.join(ROOT, p)
        if not os.path.isfile(ap):
            changed.append(f"{p} missing")
        elif sha(ap) != h:
            changed.append(f"{p} changed")
    return not changed, fr, changed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--reason", help="why a re-freeze after a test evaluation is justified (recorded)")
    ap.add_argument("--successor", action="store_true",
                    help="firmware integration change: archive the current freeze, require identical ML artifacts")
    a = ap.parse_args()
    if a.successor:
        out = successor(a.reason)
        print(f"successor freeze of {out['model_id']}: ML artifacts unchanged; changed files {out['changed_files']}; "
              f"previous record archived as {out['successor_of']}")
        return
    if a.verify:
        ok, fr, changed = verify()
        print(("frozen model %s intact" % fr["model_id"]) if ok else "NOT intact: " + "; ".join(changed))
        sys.exit(0 if ok else 1)
    out = freeze(a.reason)
    print(f"frozen {out['model_id']} ({len(out['hashes'])} files hashed, firmware.bin "
          f"{'included' if out['firmware_bin_included'] else 'NOT built'}) -> {rel(FROZEN)}")


if __name__ == "__main__":
    main()
