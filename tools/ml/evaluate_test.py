"""One-shot evaluation of the FROZEN model on the independent test session(s).

  python tools/ml/evaluate_test.py          (no hardware; no training)

Preconditions (otherwise it refuses): models/frozen.json exists and every hashed file
(model, thresholds, exported headers, training code/config, split, firmware image) is
unchanged; the test sessions in data/splits.json are the frozen ones and are complete
(session.json) real recordings.
Nothing is fitted or tuned here: the tree, the rule baseline and the label-consistency
thresholds all come from the frozen model.json.
Reports, for the decision tree and the rule baseline:
  - as collected: every valid window with its trial label
  - filtered: the frozen label-consistency rule (handling windows without motion excluded)
  - confusion matrix, accuracy, macro-F1, per-class precision/recall/F1, exclusions,
    misclassified windows, >= 10 valid windows per class check, C/Python parity on
    every test window (host build of the firmware model code)
Writes evidence/offline/test_eval_<sessions>_<UTC>.md/.json (also copied to models/<id>/).
"""
import datetime
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from sigml import CLASSES, CLASS_SHORT, baseline, dataset, metrics   # noqa: E402
import freeze as FZ                                                  # noqa: E402

REQ_PER_CLASS = 10


def tree_predict(tree, x):
    nodes = tree["nodes"]
    i = 0
    for _ in range(len(nodes)):
        n = nodes[i]
        if n["feature"] < 0:
            return n["class"]
        i = n["left"] if x[n["feature"]] <= n["threshold"] else n["right"]
    raise RuntimeError("tree walk did not terminate")


def counts(ws):
    return [sum(1 for w in ws if w.label == c) for c in range(len(CLASSES))]


def confusion_md(m):
    rows = ["| true \\ predicted | " + " | ".join(CLASS_SHORT) + " |", "|---|" + "---|" * len(CLASS_SHORT)]
    rows += [f"| {CLASS_SHORT[i]} | " + " | ".join(str(v) for v in r) + " |" for i, r in enumerate(m["confusion"])]
    return "\n".join(rows)


def per_class_md(m):
    rows = ["| class | precision | recall | F1 | support |", "|---|---|---|---|---|"]
    rows += [f"| {CLASSES[i]} | {p['precision']:.3f} | {p['recall']:.3f} | {p['f1']:.3f} | {p['support']} |"
             for i, p in enumerate(m["per_class"])]
    return "\n".join(rows)


def main():
    ok, fr, changed = FZ.verify()
    if not ok:
        sys.exit("refusing to evaluate: " + "; ".join(changed))
    with open(os.path.join(ROOT, "data", "splits.json")) as fh:
        splits = json.load(fh)
    tests = splits["test_sessions"]
    if sorted(tests) != sorted(fr["test_sessions"]):
        sys.exit(f"data/splits.json test sessions {tests} differ from the frozen ones {fr['test_sessions']}")
    raw = os.path.join(ROOT, "data", "raw")
    for s in tests:
        if not os.path.isfile(os.path.join(raw, s, "session.json")):
            sys.exit(f"test session {s} is not complete ({s}/session.json missing) - record it first")
    with open(os.path.join(ROOT, fr["model_dir"], "model.json")) as fh:
        model = json.load(fh)

    trials = [t for t in dataset.load_trials(raw) if t.session in set(tests)]
    bad = [f"{t.session}/{t.trial}" for t in trials if t.meta.get("synthetic") or t.meta.get("simulated")]
    if bad:
        sys.exit(f"synthetic/simulated trials in the test session: {bad}")
    excluded_trials = []
    for s in tests:
        for f in sorted(os.listdir(os.path.join(raw, s))):
            if f.endswith(".json") and not f.startswith("session"):
                with open(os.path.join(raw, s, f)) as fh:
                    m = json.load(fh)
                if m.get("excluded"):
                    excluded_trials.append({"trial": f"{s}/{m['trial']}", "label": m["label"],
                                            "reason": m.get("exclusion_reason", "")})
    windows, exclusions = dataset.build_windows(trials)
    quality_excl = len(exclusions)
    lc_cfg = model.get("label_consistency")
    if lc_cfg:
        _, filtered, lc = dataset.label_consistency([], windows, exclusions, thresholds=lc_cfg["thresholds"])
    else:
        filtered, lc = list(windows), None

    def evaluate(ws):
        X, y = [w.features for w in ws], [w.label for w in ws]
        tp = [tree_predict(model["tree"], x) for x in X]
        bp = baseline.predict(X, model["baseline"])
        return tp, bp, metrics.summary(y, tp), metrics.summary(y, bp)

    tp_all, bp_all, m_all, b_all = evaluate(windows)
    tp_f, bp_f, m_f, b_f = evaluate(filtered)
    req_ok = all(c >= REQ_PER_CLASS for c in counts(filtered))

    # C/Python parity on every test window, against the exported (frozen) headers
    mdir = os.path.join(ROOT, fr["model_dir"])
    tag = "+".join(tests)
    wpath = os.path.join(mdir, f"test_{tag}_windows.jsonl")
    with open(wpath, "w") as fh:
        for w, p in zip(windows, tp_all):
            fh.write(json.dumps({"id": w.wid, "label": w.label, "python_pred": p, "bias": list(w.gyro_bias),
                                 "features": w.features, "raw": w.raw}) + "\n")
    r = subprocess.run([sys.executable, os.path.join(HERE, "verify_c_model.py"), "--model-dir", mdir,
                        "--windows", wpath, "--tag", tag, "--windows-from", f"independent test sessions {tag}"],
                       cwd=ROOT, capture_output=True, text=True)
    try:
        par = json.loads(r.stdout[r.stdout.index("{"):])
    except ValueError:
        par = {"all_match": False, "error": (r.stdout + r.stderr)[-400:]}
    still_ok, _, changed_after = FZ.verify()

    gate2 = req_ok and par.get("all_match") and still_ok and m_f["macro_f1"] >= b_f["macro_f1"]
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    errors = [{"window": w.wid, "true": CLASSES[w.label], "tree": CLASSES[p], "baseline": CLASSES[b]}
              for w, p, b in zip(filtered, tp_f, bp_f) if p != w.label]
    res = {
        "utc": stamp, "model_id": model["model_id"], "frozen_utc": fr["frozen_utc"], "test_sessions": tests,
        "test_sessions_present_at_freeze": fr.get("test_sessions_present_at_freeze", []),
        "freeze_intact_before_and_after": still_ok, "changed_after": changed_after,
        "trials": len(trials), "excluded_trials": excluded_trials,
        "windows_as_collected": counts(windows), "windows_filtered": counts(filtered),
        "excluded_windows_quality": quality_excl, "label_consistency": lc and {
            "thresholds": lc["thresholds"], "excluded_windows": lc["excluded_windows"],
            "per_trial": lc["per_trial"], "trials_label_not_performed": lc["trials_label_not_performed"]},
        "requirement_10_per_class_filtered": req_ok,
        "as_collected": {"tree": m_all, "baseline": b_all}, "filtered": {"tree": m_f, "baseline": b_f},
        "misclassified_filtered": errors, "c_parity": {k: par.get(k) for k in
                                                      ("windows", "prediction_agreement", "features_bit_exact",
                                                       "baseline_agreement", "all_match", "error")},
        "gate2_ready": bool(gate2),
    }
    base = os.path.join(ROOT, "evidence", "offline", f"test_eval_{tag}_{stamp}")
    with open(base + ".json", "w") as fh:
        json.dump(res, fh, indent=1)

    md = [f"# Independent test evaluation - {tag} - model `{model['model_id']}`\n",
          f"Model frozen {fr['frozen_utc']} (models/frozen.json); freeze intact before and after this run: "
          f"**{'yes' if still_ok else 'NO: ' + '; '.join(changed_after)}**. Test sessions present at freeze time: "
          f"{fr.get('test_sessions_present_at_freeze') or 'none'}. Nothing was trained or tuned on {tag}.\n",
          "## Data\n",
          f"- trials used: {len(trials)}; excluded trials (kept on disk): "
          + ("; ".join(f"{e['trial']} ({e['reason']})" for e in excluded_trials) or "none"),
          f"- windows as collected {dict(zip(CLASS_SHORT, counts(windows)))}; after the frozen label-consistency "
          f"rule {dict(zip(CLASS_SHORT, counts(filtered)))}; windows excluded for sample quality: {quality_excl}",
          f"- >= {REQ_PER_CLASS} valid filtered windows per class: **{'met' if req_ok else 'NOT met'}**"]
    if lc:
        md += [f"- frozen thresholds {lc['thresholds']}; handling windows without motion excluded: "
               f"{lc['excluded_windows']}; per trial kept/total: "
               + ", ".join(f"{k} {v['active']}/{v['windows']}" for k, v in sorted(lc["per_trial"].items())
                           if v["label"] != 0)]
    for name, mt, mb in (("filtered (frozen label-consistency rule)", m_f, b_f), ("as collected", m_all, b_all)):
        md += [f"\n## {name.capitalize()}\n",
               f"| | decision tree | rule baseline |\n|---|---|---|\n"
               f"| accuracy | {mt['accuracy']:.3f} | {mb['accuracy']:.3f} |\n"
               f"| macro-F1 | {mt['macro_f1']:.3f} | {mb['macro_f1']:.3f} |\n| n | {mt['n']} | {mb['n']} |\n",
               "Decision tree:\n", confusion_md(mt) + "\n", per_class_md(mt) + "\n",
               "Rule baseline:\n", confusion_md(mb) + "\n", per_class_md(mb) + "\n"]
    md += [f"\n## Misclassified filtered windows ({len(errors)})\n"]
    md += [f"- {e['window']}: true {e['true']}, tree {e['tree']}, baseline {e['baseline']}" for e in errors[:60]]
    md += [f"\n## C/Python parity on {tag}\n",
           f"{par.get('prediction_agreement')}/{par.get('windows')} classes, features bit-exact "
           f"{par.get('features_bit_exact')}/{par.get('windows')}, baseline {par.get('baseline_agreement')}/"
           f"{par.get('windows')}: **{'PASS' if par.get('all_match') else 'FAIL'}**\n",
           f"## Gate 2 model readiness: **{'YES' if gate2 else 'NO'}** "
           "(>= 10 windows/class, parity PASS, freeze intact, tree macro-F1 >= baseline on the filtered set)\n"]
    with open(base + ".md", "w", encoding="utf-8") as fh:
        fh.write("\n".join(md) + "\n")
    shutil.copy(base + ".md", os.path.join(mdir, f"test_report_{tag}.md"))
    print("\n".join(md[:8]))
    print(f"filtered: tree acc {m_f['accuracy']:.3f} macroF1 {m_f['macro_f1']:.3f} | baseline acc "
          f"{b_f['accuracy']:.3f} macroF1 {b_f['macro_f1']:.3f}")
    print(f"as collected: tree acc {m_all['accuracy']:.3f} macroF1 {m_all['macro_f1']:.3f} | baseline acc "
          f"{b_all['accuracy']:.3f} macroF1 {b_all['macro_f1']:.3f}")
    print(f"parity {'PASS' if par.get('all_match') else 'FAIL'}; gate 2 model ready: {'YES' if gate2 else 'NO'}")
    print(f"written: {os.path.relpath(base, ROOT)}.md")
    sys.exit(0 if gate2 else 1)


if __name__ == "__main__":
    main()
