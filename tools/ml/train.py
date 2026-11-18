"""Train, evaluate and export the Smart Item Guardian handling classifier.

Usage (PlatformIO's Python 3.11 is sufficient; no third-party packages):
  python tools/ml/train.py --data data/raw --splits data/splits.json
  python tools/ml/train.py --data data/synthetic --splits data/splits_synthetic.json --placeholder

Pipeline
  1. load trials, cut 2 s non-overlapping windows (1 s trimmed per trial end),
     exclude windows with invalid samples or > 2 missed samples (logged)
  2. split by SESSION (data/splits.json); refuse overlapping sessions
  3. choose max_depth / min_samples_leaf by leave-one-session-out CV on the
     TRAINING sessions only (macro-F1, ties -> simpler tree)
  4. fit the tree and the rule baseline on all training windows
  5. evaluate both on the held-out TEST sessions: confusion matrix,
     per-class precision/recall/F1, misclassified windows
  6. write models/<model_id>/ (model.json, report.md/json, predictions,
     held-out windows for C verification) and src/model/*.h
"""
import argparse
import csv
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sigml import CLASSES, CLASS_SHORT                        # noqa: E402
from sigml import baseline, dataset, export, metrics           # noqa: E402
from sigml import features as F                                # noqa: E402
from sigml.cart import DecisionTree                            # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_GRID = {"max_depth": [2, 3, 4, 5], "min_samples_leaf": [2, 4, 8]}
REQ_TRAIN_PER_CLASS = 30
REQ_TEST_PER_CLASS = 10


def counts_by_class(ws):
    c = [0] * len(CLASSES)
    for w in ws:
        c[w.label] += 1
    return c


def loso_cv(train, grid):
    sessions = sorted({w.session for w in train})
    results = []
    for depth in grid["max_depth"]:
        for leaf in grid["min_samples_leaf"]:
            if len(sessions) < 2:
                results.append({"max_depth": depth, "min_samples_leaf": leaf, "cv_macro_f1": None, "folds": []})
                continue
            folds, yt, yp = [], [], []
            for s in sessions:
                tr = [w for w in train if w.session != s]
                va = [w for w in train if w.session == s]
                t = DecisionTree(depth, leaf).fit([w.features for w in tr], [w.label for w in tr])
                pred = t.predict([w.features for w in va])
                m = metrics.summary([w.label for w in va], pred)
                folds.append({"held_out_session": s, "macro_f1": m["macro_f1"], "accuracy": m["accuracy"], "n": m["n"]})
                yt += [w.label for w in va]
                yp += pred
            pooled = metrics.summary(yt, yp)
            mean = sum(f["macro_f1"] for f in folds) / len(folds)
            results.append({"max_depth": depth, "min_samples_leaf": leaf, "cv_macro_f1": mean,
                            "cv_pooled_macro_f1": pooled["macro_f1"], "folds": folds})
    scored = [r for r in results if r["cv_macro_f1"] is not None]
    if scored:
        best = max(scored, key=lambda r: (round(r["cv_macro_f1"], 6), -r["max_depth"], r["min_samples_leaf"]))
    else:
        best = {"max_depth": 3, "min_samples_leaf": 4}
    return best, results


def pick_golden(test, preds, max_n):
    per = max(1, max_n // len(CLASSES))
    out = []
    for c in range(len(CLASSES)):
        cand = [(w, p) for w, p in zip(test, preds) if w.label == c]
        step = max(1, len(cand) // per)
        out += cand[::step][:per]
    return out


def fmt_pc(pc):
    rows = ["| class | precision | recall | F1 | support |", "|---|---|---|---|---|"]
    for i, p in enumerate(pc):
        rows.append(f"| {CLASSES[i]} | {p['precision']:.3f} | {p['recall']:.3f} | {p['f1']:.3f} | {p['support']} |")
    return "\n".join(rows)


def md_confusion(m):
    rows = ["| true \\ predicted | " + " | ".join(CLASS_SHORT) + " |", "|---|" + "---|" * len(CLASS_SHORT)]
    for i, r in enumerate(m):
        rows.append(f"| {CLASS_SHORT[i]} | " + " | ".join(str(v) for v in r) + " |")
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=os.path.join(ROOT, "data", "raw"))
    ap.add_argument("--splits", default=os.path.join(ROOT, "data", "splits.json"))
    ap.add_argument("--models", default=os.path.join(ROOT, "models"))
    ap.add_argument("--export", default=os.path.join(ROOT, "src", "model"))
    ap.add_argument("--config", default=os.path.join(ROOT, "tools", "ml", "training_config.json"))
    ap.add_argument("--placeholder", action="store_true", help="mark the exported model as a placeholder")
    ap.add_argument("--golden-max", type=int, default=30)
    ap.add_argument("--no-export", action="store_true")
    args = ap.parse_args()

    cfg = {"grid": DEFAULT_GRID}
    if os.path.isfile(args.config):
        with open(args.config, encoding="utf-8") as fh:
            cfg.update(json.load(fh))
    t0 = time.time()
    trials = dataset.load_trials(args.data)
    if not trials:
        sys.exit(f"no trials found under {args.data}")
    # synthetic (generated) or simulated-board trials can never yield a real model
    synthetic = any(t.meta.get("synthetic") or t.meta.get("simulated") for t in trials)
    placeholder = args.placeholder or synthetic
    splits = dataset.load_splits(args.splits)
    role = "validation" if splits.get("validation_sessions") else "held-out test"
    if splits.get("validation_sessions"):
        # test sessions are reserved for tools/ml/evaluate_test.py: never loaded during training
        trials = [t for t in trials if t.session not in set(splits["test_sessions"])]
        synthetic = any(t.meta.get("synthetic") or t.meta.get("simulated") for t in trials)
        placeholder = args.placeholder or synthetic
    windows, exclusions = dataset.build_windows(trials)
    train, test, unused = dataset.split_windows(windows, splits)
    if not train or not test:
        sys.exit("training or test split is empty - check data/splits.json")
    test_raw = test
    lc = None
    if cfg.get("label_consistency"):
        train, test, lc = dataset.label_consistency(train, test, exclusions)

    tr_counts, te_counts = counts_by_class(train), counts_by_class(test)
    req = {
        "train_windows_per_class": tr_counts, "test_windows_per_class": te_counts,
        "train_min_required": REQ_TRAIN_PER_CLASS, "test_min_required": REQ_TEST_PER_CLASS,
        "train_ok": all(c >= REQ_TRAIN_PER_CLASS for c in tr_counts),
        "test_ok": all(c >= REQ_TEST_PER_CLASS for c in te_counts),
        "train_sessions": sorted({w.session for w in train}), "test_sessions": sorted({w.session for w in test}),
        "session_overlap": sorted({w.session for w in train} & {w.session for w in test}),
        "unused_sessions": unused, "eval_role": role,
        "reserved_test_sessions": splits["test_sessions"] if splits.get("validation_sessions") else [],
    }

    best, cv = loso_cv(train, cfg["grid"])
    Xtr, ytr = [w.features for w in train], [w.label for w in train]
    Xte, yte = [w.features for w in test], [w.label for w in test]
    tree = DecisionTree(best["max_depth"], best["min_samples_leaf"]).fit(Xtr, ytr)
    base_p = baseline.fit(Xtr, ytr)

    pred_tr, pred_te = tree.predict(Xtr), tree.predict(Xte)
    base_tr, base_te = baseline.predict(Xtr, base_p), baseline.predict(Xte, base_p)
    m_tr, m_te = metrics.summary(ytr, pred_tr), metrics.summary(yte, pred_te)
    b_tr, b_te = metrics.summary(ytr, base_tr), metrics.summary(yte, base_te)
    Xraw, yraw = [w.features for w in test_raw], [w.label for w in test_raw]
    pred_raw = tree.predict(Xraw)
    m_raw, b_raw = metrics.summary(yraw, pred_raw), metrics.summary(yraw, baseline.predict(Xraw, base_p))

    data_hash = hashlib.sha256("".join(sorted(t.sha256 for t in trials)).encode()).hexdigest()[:16]
    tdict = tree.to_dict(F.FEATURE_NAMES)
    model_id = ("ph-" if placeholder else "dt-") + hashlib.sha256(
        (json.dumps(tdict, sort_keys=True) + data_hash).encode()).hexdigest()[:10]
    out = os.path.join(args.models, model_id)
    os.makedirs(out, exist_ok=True)

    errors = [{"window": w.wid, "true": CLASSES[w.label], "tree": CLASSES[p], "baseline": CLASSES[b],
               **{n: round(v, 5) for n, v in zip(F.FEATURE_NAMES, w.features)}}
              for w, p, b in zip(test, pred_te, base_te) if p != w.label]

    model = {
        "model_id": model_id, "placeholder": placeholder, "synthetic_data": synthetic,
        "classes": list(CLASSES), "features": list(F.FEATURE_NAMES),
        "used_features": [F.FEATURE_NAMES[i] for i in tree.used_features()],
        "preprocessing": {"fs_hz": F.FS_HZ, "window_samples": F.WINDOW_SAMPLES, "hop_samples_online": F.HOP_SAMPLES,
                          "acc_lsb_per_g": F.ACC_LSB_PER_G, "gyro_lsb_per_dps": F.GYRO_LSB_PER_DPS,
                          "high_g_dev": F.HIGH_G_DEV, "trim_s": dataset.TRIM_S,
                          "max_gap_samples": dataset.MAX_GAP_SAMPLES, "arithmetic": "binary32, see features.c"},
        "hyperparameters": {"max_depth": best["max_depth"], "min_samples_leaf": best["min_samples_leaf"],
                            "selection": "leave-one-session-out CV on training sessions, macro-F1"},
        "tree": tdict, "baseline": base_p, "data_hash": data_hash,
        "label_consistency": ({k: lc[k] for k in ("rule", "thresholds", "min_active_windows")} if lc else None),
        "sessions": {"train": req["train_sessions"], role.replace("held-out ", ""): req["test_sessions"]},
        "python": sys.version.split()[0],
    }
    report = {
        "model_id": model_id, "placeholder": placeholder, "synthetic_data": synthetic,
        "requirements": req, "cv": cv,
        "train": {"tree": m_tr, "baseline": b_tr}, "test": {"tree": m_te, "baseline": b_te},
        "test_uncleaned": {"tree": m_raw, "baseline": b_raw}, "label_consistency": lc,
        "misclassified_test_windows": errors, "exclusions": exclusions,
        "n_windows": len(windows), "elapsed_s": round(time.time() - t0, 1),
    }
    with open(os.path.join(out, "model.json"), "w") as fh:
        json.dump(model, fh, indent=1)
    with open(os.path.join(out, "report.json"), "w") as fh:
        json.dump(report, fh, indent=1)
    with open(os.path.join(out, "test_predictions.csv"), "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["window", "session", "label", "tree_pred", "baseline_pred"] + list(F.FEATURE_NAMES))
        for w, p, b in zip(test, pred_te, base_te):
            wr.writerow([w.wid, w.session, CLASSES[w.label], CLASSES[p], CLASSES[b]] + ["%.9g" % v for v in w.features])
    with open(os.path.join(out, "heldout_windows.jsonl"), "w") as fh:
        for w, p in zip(test_raw, pred_raw):        # all held-out windows: widest C/Python parity coverage
            fh.write(json.dumps({"id": w.wid, "label": w.label, "python_pred": p, "bias": list(w.gyro_bias),
                                 "features": w.features, "raw": w.raw}) + "\n")
    with open(os.path.join(out, "data_manifest.csv"), "w", newline="") as fh:
        rows = dataset.manifest_rows(trials, windows, splits)
        wr = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        wr.writeheader()
        wr.writerows(rows)

    banner = ("> **PLACEHOLDER MODEL - trained on SYNTHETIC data for offline plumbing only. "
              "These numbers are not results and must not be reported as such.**\n\n" if placeholder else "")
    md = [f"# Model report `{model_id}`\n", banner,
          f"Data hash `{data_hash}` - {len(trials)} trials, {len(windows)} valid windows, "
          f"{len(exclusions)} excluded windows.\n",
          "## Dataset requirement check\n",
          f"- training windows per class {dict(zip(CLASS_SHORT, tr_counts))} (need >= {REQ_TRAIN_PER_CLASS}): "
          f"**{'met' if req['train_ok'] else 'NOT met'}**",
          f"- {role} windows per class {dict(zip(CLASS_SHORT, te_counts))} (need >= {REQ_TEST_PER_CLASS}): "
          f"**{'met' if req['test_ok'] else 'NOT met'}**",
          f"- train sessions {req['train_sessions']}, {role} sessions {req['test_sessions']}, "
          f"overlap {req['session_overlap']}"
          + (f"; test sessions {req['reserved_test_sessions']} reserved for tools/ml/evaluate_test.py "
             "(not loaded here)" if req["reserved_test_sessions"] else "") + "\n"]
    if lc:
        md += ["## Label consistency (handling windows without handling motion)\n",
               f"Rule: {lc['rule']}; thresholds from training STATIONARY windows {lc['thresholds']}. "
               f"Excluded {lc['excluded_windows']} handling-labelled windows (never relabelled). "
               "Per trial, kept/total:\n",
               "| trial | label | kept / total |", "|---|---|---|"]
        md += [f"| {k} | {CLASSES[v['label']]} | {v['active']} / {v['windows']} |"
               for k, v in sorted(lc["per_trial"].items()) if v["active"] < v["windows"]]
        md += ["", "Trials whose labelled activity was essentially not performed "
               f"(< {lc['min_active_windows']} active windows): "
               f"{', '.join(lc['trials_label_not_performed']) or 'none'}\n"]
    md += ["## Selected model\n",
          f"max_depth={best['max_depth']}, min_samples_leaf={best['min_samples_leaf']} -> depth {tree.depth}, "
          f"{len(tree.nodes)} nodes, uses {model['used_features']}\n",
          "```\n" + tree.describe(F.FEATURE_NAMES, CLASSES) + "\n```\n",
          f"## {role.capitalize()} sessions {req['test_sessions']} - decision tree\n",
          f"accuracy {m_te['accuracy']:.3f}, macro-F1 {m_te['macro_f1']:.3f}, n={m_te['n']}\n",
          md_confusion(m_te["confusion"]) + "\n", fmt_pc(m_te["per_class"]) + "\n",
          f"## {role.capitalize()} sessions - rule baseline\n",
          f"thresholds {json.dumps({k: round(v, 5) for k, v in base_p.items()})}\n",
          f"accuracy {b_te['accuracy']:.3f}, macro-F1 {b_te['macro_f1']:.3f}, n={b_te['n']}\n",
          md_confusion(b_te["confusion"]) + "\n", fmt_pc(b_te["per_class"]) + "\n",
          f"## {role.capitalize()} sessions - as collected (no label-consistency exclusion)\n",
          f"Every {role} window with its trial label, including windows in which the operator had stopped "
          "handling the box (those are expected to be predicted STATIONARY).\n",
          f"tree accuracy {m_raw['accuracy']:.3f}, macro-F1 {m_raw['macro_f1']:.3f}, n={m_raw['n']}; "
          f"baseline accuracy {b_raw['accuracy']:.3f}, macro-F1 {b_raw['macro_f1']:.3f}\n",
          md_confusion(m_raw["confusion"]) + "\n",
          "## Training sessions (for reference, optimistic)\n",
          f"tree accuracy {m_tr['accuracy']:.3f} / macro-F1 {m_tr['macro_f1']:.3f}; "
          f"baseline accuracy {b_tr['accuracy']:.3f} / macro-F1 {b_tr['macro_f1']:.3f}\n",
          f"## Misclassified {role} windows ({len(errors)})\n"]
    md += [f"- {e['window']}: true {e['true']}, tree {e['tree']}, baseline {e['baseline']}" for e in errors[:40]]
    with open(os.path.join(out, "report.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(md) + "\n")
    with open(os.path.join(args.models, "latest.json"), "w") as fh:
        json.dump({"model_id": model_id, "placeholder": placeholder, "path": os.path.relpath(out, ROOT).replace("\\", "/")}, fh, indent=1)

    if not args.no_export:
        golden = pick_golden(test, pred_te, args.golden_max)
        src = ("SYNTHETIC placeholder held-out sessions " if placeholder else f"{role} sessions ") + \
              "+".join(req["test_sessions"])
        export.write_all(args.export, tree, base_p, golden, model_id, placeholder, data_hash, src)

    print(f"model {model_id} placeholder={placeholder} depth={tree.depth} nodes={len(tree.nodes)}")
    print(f"[{role}] tree acc={m_te['accuracy']:.3f} macroF1={m_te['macro_f1']:.3f} | "
          f"baseline acc={b_te['accuracy']:.3f} macroF1={b_te['macro_f1']:.3f}")
    if lc:
        print(f"label consistency: {lc['excluded_windows']} handling windows without motion excluded; "
              f"as-collected {role} tree acc={m_raw['accuracy']:.3f} macroF1={m_raw['macro_f1']:.3f}; "
              f"activity not performed: {lc['trials_label_not_performed'] or 'none'}")
    print(f"requirements train_ok={req['train_ok']} test_ok={req['test_ok']} -> {os.path.relpath(out, ROOT)}")


if __name__ == "__main__":
    main()
