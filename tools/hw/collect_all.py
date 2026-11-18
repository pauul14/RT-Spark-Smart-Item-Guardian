"""Hardware session 1, data part: all planned recording sessions, then the offline pipeline.

  python tools/hw/collect_all.py [--port COMx] [--only S03,S04] [--skip-pipeline] [--sim]

For each session in data/collection_plan.json that is not finished yet
(data/raw/<S>/session.json missing), it runs tools/hw/capture.py with the planned
placement/surface and the board-UI operator mode: the setup instruction shows on the
LCD and this terminal, and the session starts when the operator presses UP (the board
is open at that moment). Trials then run on beeps without key presses.
Afterwards tools/ml/post_capture.py trains, verifies and rebuilds (no hardware).
Re-running continues with the unfinished sessions only.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PY = sys.executable


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--only", help="comma-separated session ids")
    ap.add_argument("--skip-pipeline", action="store_true")
    ap.add_argument("--sim", action="store_true", help="dry run against the simulated board (data/sim, no pipeline)")
    ap.add_argument("--ui", choices=("board", "terminal"), default="board",
                    help="terminal: LCD prompts + every confirmation typed in this terminal (no board keys)")
    ap.add_argument("--start-delay", type=float, default=0,
                    help="key-free sessions: show each set-up with this countdown instead of waiting for UP")
    a = ap.parse_args()
    with open(os.path.join(ROOT, "data", "collection_plan.json")) as fh:
        plan = json.load(fh)
    with open(os.path.join(ROOT, "data", "splits.json")) as fh:
        splits = json.load(fh)
    only = set(a.only.split(",")) if a.only else None
    root = os.path.join(ROOT, "data", "sim" if a.sim else "raw")
    for s in plan["sessions"]:
        sid = s["session"]
        want = {"train": "train_sessions", "validation": "validation_sessions", "test": "test_sessions"}[s["split"]]
        if sid not in splits.get(want, []):
            sys.exit(f"{sid} is planned as {s['split']} but data/splits.json does not list it in {want}")
        if only and sid not in only:
            continue
        if os.path.isfile(os.path.join(root, sid, "session.json")):
            print(f"{sid}: already finished - skipped")
            continue
        print(f"\n=================== {sid} ({s['split']}) ===================\n{s['setup']}")
        cmd = [PY, os.path.join(HERE, "capture.py"), "--session", sid, "--operator", s["operator"],
               "--placement", s["placement"], "--surface", s["surface"], "--setup", s["setup"],
               "--trials-per-class", str(s.get("trials_per_class", plan["trials_per_class"])),
               "--seconds", str(plan["seconds"]), "--ui", a.ui,
               "--labels", ",".join(s.get("labels", ["STATIONARY", "NORMAL_HANDLING", "ROUGH_HANDLING"]))]
        if a.port:
            cmd += ["--port", a.port]
        if a.start_delay > 0 and a.ui == "board":
            cmd += ["--start-delay", str(a.start_delay)]
        if a.sim:
            cmd.append("--sim")
        rc = subprocess.call(cmd, cwd=ROOT)
        if rc != 0 or not os.path.isfile(os.path.join(root, sid, "session.json")):
            sys.exit(f"{sid} did not finish (exit {rc}). Fix the cause, then re-run this command to continue.")
    if a.sim or a.skip_pipeline:
        print("\ncollection done (pipeline skipped)")
        return
    sys.exit(subprocess.call([PY, os.path.join(ROOT, "tools", "ml", "post_capture.py")], cwd=ROOT))


if __name__ == "__main__":
    main()
