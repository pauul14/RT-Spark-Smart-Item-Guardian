"""Round 2 before gate 2, in ONE terminal run (typed confirmations, no board keys):

  python tools/hw/round2.py [--port COM3]

  1. S05  NORMAL-only training session            (skipped if data/raw/S05/session.json exists)
  2. train on S01-S03+S05, validate on S04, export, C/Python parity, offline checks + build, FREEZE
                                                    (skipped if models/frozen.json is intact and newer
                                                     than every training session)
  3. S06  new independent test session, all three classes  (skipped if already complete)
  4. tools/ml/evaluate_test.py: frozen model on S06, no retuning

Stops at the first failure with the reason; re-running continues where it stopped.
Everything printed here is also written to evidence/offline/round2_<UTC>.log.
No flashing, reset or power action is performed by any step.
"""
import argparse
import datetime
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "ml"))
PY = sys.executable
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
LOG = os.path.join(ROOT, "evidence", "offline", f"round2_{STAMP}.log")


def log(text):
    sys.stdout.write(text)
    sys.stdout.flush()
    with open(LOG, "a", encoding="utf-8") as fh:
        fh.write(text)


def banner(text):
    log(f"\n{'#' * 70}\n# {text}\n{'#' * 70}\n")


def run(args):
    """Run a child with this terminal's stdin (typed answers) and stream its output here + log."""
    p = subprocess.Popen([PY, "-u"] + args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    fd = p.stdout.fileno()
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        log(chunk.decode("utf-8", "replace").replace("\r\n", "\n"))
    return p.wait()


def lcd(port, text, secs):
    try:
        from serial_common import Board, find_port
        b = Board(find_port(port), os.path.join(ROOT, "evidence", "offline", f"round2_{STAMP}_serial.log"))
        b.command(f"say {secs} {text[:80]}", r"^#PROMPT,", 3)
        b.close()
    except Exception as e:                                  # LCD text is a convenience only
        log(f"(LCD message not shown: {e})\n")


def done(session):
    return os.path.isfile(os.path.join(ROOT, "data", "raw", session, "session.json"))


def frozen_current():
    import freeze as FZ
    ok, fr, _ = FZ.verify()
    if not ok:
        return False
    t = os.path.getmtime(FZ.FROZEN)
    return all(os.path.getmtime(os.path.join(ROOT, "data", "raw", s, "session.json")) < t
               for s in fr["train_sessions"] + fr["validation_sessions"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM3")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    collect = [os.path.join("tools", "hw", "collect_all.py"), "--ui", "terminal", "--skip-pipeline", "--port", a.port]

    banner("STEP 1/4  S05 - NORMAL-only training session (4 trials x 25 s)")
    if done("S05"):
        log("S05 already complete - not repeated\n")
    elif run(collect + ["--only", "S05"]) != 0 or not done("S05"):
        log("\nSTOPPED: S05 is not complete. Re-run this command to continue it.\n")
        sys.exit(1)

    banner("STEP 2/4  train (S01-S03+S05), validate (S04), export, parity, build, FREEZE  (~3-5 min, no hardware)")
    if frozen_current():
        log("frozen model is intact and newer than all training data - not repeated\n")
    else:
        lcd(a.port, "PC is TRAINING (3-5 min). Board can rest. Wait for S06 steps on PC.", 600)
        rc = run([os.path.join("tools", "ml", "post_capture.py")])
        if rc != 0 or not frozen_current():
            lcd(a.port, "STOPPED: training step failed. Nothing recorded. See PC.", 600)
            log("\nSTOPPED: training/verification/freeze did not complete - see the lines above.\n")
            sys.exit(1)
    with open(os.path.join(ROOT, "models", "frozen.json")) as fh:
        fr = json.load(fh)
    log(f"\nMODEL FROZEN: {fr['model_id']} at {fr['frozen_utc']} - S06 cannot change it.\n")

    banner("STEP 3/4  S06 - NEW independent test session (all three classes, 6 trials x 25 s)")
    if done("S06"):
        log("S06 already complete - not repeated\n")
    elif run(collect + ["--only", "S06"]) != 0 or not done("S06"):
        log("\nSTOPPED: S06 is not complete. Re-run this command to continue it.\n")
        sys.exit(1)

    banner("STEP 4/4  evaluate the FROZEN model on S06 (no retuning)")
    rc = run([os.path.join("tools", "ml", "evaluate_test.py")])
    lcd(a.port, "ROUND 2 FINISHED. Results on PC. Wait for next instructions.", 600)
    banner("ROUND 2 FINISHED - tell the assistant 'round 2 done'" if rc in (0, 1) else "EVALUATION ERROR")
    log(f"log: {os.path.relpath(LOG, ROOT)}\n")
    sys.exit(0)


if __name__ == "__main__":
    main()
