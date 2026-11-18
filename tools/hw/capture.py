"""Guided, labelled IMU data collection (hardware session 1).

  python tools/hw/capture.py --session S01 --operator P1 --placement bare-board-handheld --surface wood
  python tools/hw/capture.py --session S04 --operator P1 --placement bare-board-handheld --surface laminate   (test session)
  python tools/hw/capture.py --session SIMTEST --sim        (offline dry run, writes to data/sim/)

One trial = one label. The tool prints the safe protocol for the label, starts
the board's raw 100 Hz stream ("cap start"), records for --seconds, stops it,
checks sequence continuity and invalid flags, asks the operator whether the
action was performed as instructed, and saves:
  data/raw/<session>/<LABEL>_<nn>.csv   seq,t_us,ax,ay,az,gx,gy,gz,flags  (board raw counts)
  data/raw/<session>/<LABEL>_<nn>.json  label, gyro bias from the board, timing, quality, operator notes
  data/raw/<session>/session.json, session_serial.log
Trials the operator rejects are kept with "excluded": true (audit trail) and repeated.
No personal data is recorded: the operator is an anonymous id (P1, P2, ...).
"""
import argparse
import datetime
import json
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.stdout.reconfigure(line_buffering=True)   # progress visible when run under collect_all
from serial_common import Board, Operator, ask, find_port, parse_kv, utc_now   # noqa: E402

LABELS = ("STATIONARY", "NORMAL_HANDLING", "ROUGH_HANDLING")
PROTOCOL = {
    "STATIONARY": [
        "Put the board down on the table and take your hands away.",
        "Do not touch the board or the table for the whole trial (normal room activity is fine).",
        "Vary between trials: board flat / tilted on a support (note it when asked).",
    ],
    "NORMAL_HANDLING": [
        "Handle the board CALMLY for the whole trial, e.g.:",
        "lift it, carry it while walking at normal pace, turn it to look at it,",
        "set it down gently, slide it across the table, lift it again. No jerks.",
    ],
    "ROUGH_HANDLING": [
        "SAFE simulation only - hold the board FIRMLY with BOTH hands the whole time:",
        "shake it vigorously (about 3-5 shakes per second) for 2-3 s, pause,",
        "do abrupt jerk-and-stop movements and fast twists, repeat until the time ends.",
        "NEVER drop, throw, strike or bang the board. Keep the USB cable slack and",
        "hold it together with the board. Stop immediately if the cable tightens.",
    ],
}


# Short LCD versions (4 x 20 characters), ACTION FIRST, shown in --ui board / terminal mode.
LCD_PROTOCOL = {
    "STATIONARY": "HANDS OFF. Board still on table. Don't touch.",
    "NORMAL_HANDLING": "HANDLE GENTLY nonstop: lift, carry, turn, set down.",
    "ROUGH_HANDLING": "SHAKE + JERK FIRMLY, both hands. NEVER drop/hit.",
}
# Varied gentle handling: NORMAL trial n uses NORMAL_VARIANTS[(n - 1) % len], so a NORMAL-only
# session covers different everyday movements instead of repeating one.
NORMAL_VARIANTS = [
    ("lift-carry", "CARRY: lift, move slowly around, within cable reach."),
    ("turn-inspect", "TURN + LOOK: lift, slowly turn board to all sides."),
    ("slide-reposition", "SLIDE + MOVE: slide board, lift, set down, again."),
    ("pick-place-mixed", "PICK UP + PUT DOWN gently, carry, turn. Nonstop."),
]
LCD_WAIT = "Type y on PC. NEXT: "          # exactly one 20-character LCD row
assert all(len(LCD_WAIT + t) <= 80 and len(t + " STOP at beep") <= 80
           for t in list(LCD_PROTOCOL.values()) + [v[1] for v in NORMAL_VARIANTS])
COVERAGE_MIN = 0.8        # handling trials: >= 80 % of windows must show motion, else automatic redo
MAX_REDO = 3


def lcd_action(label, n):
    if label == "NORMAL_HANDLING":
        return NORMAL_VARIANTS[(n - 1) % len(NORMAL_VARIANTS)][1]
    return LCD_PROTOCOL[label]


def plan(trials_per_class, session, seed=None, labels=LABELS):
    order = [lab for lab in labels for _ in range(trials_per_class)]
    random.Random(seed if seed is not None else session).shuffle(order)
    return order


def quality(rows):
    if not rows:
        return {"samples": 0, "gaps": 0, "missing": 0, "invalid": 0, "ok": False}
    gaps = missing = 0
    for a, b in zip(rows, rows[1:]):
        d = b[0] - a[0]
        if d != 1:
            gaps += 1
            missing += max(0, d - 1)
    invalid = sum(1 for r in rows if r[8] & 1)
    dur = ((rows[-1][1] - rows[0][1]) & 0xFFFFFFFF) / 1e6
    rate = (len(rows) - 1) / dur if dur > 0 else 0.0
    return {"samples": len(rows), "gaps": gaps, "missing": missing, "invalid": invalid,
            "duration_s": round(dur, 3), "effective_rate_hz": round(rate, 2),
            "ok": missing <= 2 and invalid == 0 and len(rows) >= 300}


ACTIVE_GYRO_DPS, ACTIVE_ACC_STD_G = 2.5, 0.012   # just above the S01-S03 STATIONARY maxima (2.11 dps, 0.0115 g)


def activity(rows, bias):
    """Handling coverage: how many training windows (2 s, 1 s trimmed per end) show motion.
    Mirrors sigml.dataset.label_consistency so the operator learns at once that a
    NORMAL/ROUGH trial contained rest periods (those windows are excluded in training)."""
    sys.path.insert(0, os.path.join(ROOT, "tools", "ml"))
    from sigml import features as F                 # noqa: E402  (stdlib-only package)
    gi, ai = F.FEATURE_NAMES.index("gyro_max"), F.FEATURE_NAMES.index("acc_std")
    trim, n = F.FS_HZ, F.WINDOW_SAMPLES
    body = [r[2:8] for r in rows[trim:len(rows) - trim]]
    marks = ""
    for k in range(0, len(body) - n + 1, n):
        f = F.compute(body[k:k + n], bias)
        marks += "#" if f[gi] > ACTIVE_GYRO_DPS or f[ai] > ACTIVE_ACC_STD_G else "."
    return {"active_windows": marks.count("#"), "windows": len(marks), "timeline": marks}


def record_trial(board, label, session, trial, seconds, sim):
    if sim:
        board.activity = label
    hdr = board.command(f"cap start {label} {session} {trial}", r"^#CAP,START", timeout=3)
    if not hdr:
        return None, None, "no #CAP,START from board"
    m0 = board.mark()
    t0 = utc_now()
    if sim:
        board.sleep(seconds)
    else:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            left = end - time.monotonic()
            print(f"\r  recording... {left:4.1f} s left ", end="", flush=True)
            time.sleep(0.2)
        print()
    stop = board.command("cap stop", r"^#CAP,STOP", timeout=3)
    rows = []
    for _, _, text in board.since(m0, "S,"):
        p = text.split(",")
        if len(p) == 10:
            try:
                rows.append(tuple(int(v) for v in p[1:]))
            except ValueError:
                pass
    hk = parse_kv(hdr[2])
    bias = [float(v) for v in hk.get("gyro_bias", "0;0;0").split(";")]
    meta = {"label": label, "session": session, "trial": f"{label}_{trial:02d}", "gyro_bias": bias,
            "bias_ok": hk.get("bias_ok") == "1", "fs_hz": int(hk.get("fs_hz", 100)), "fw": hk.get("fw"),
            "model_on_board": hk.get("model"), "recorded_utc": t0, "requested_seconds": seconds,
            "stop_line": stop[2] if stop else None, "synthetic": False, "simulated": bool(sim)}
    return rows, meta, None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--session", required=True, help="e.g. S01 (train), S04 (test) - one id per recording session")
    ap.add_argument("--operator", default="P1", help="anonymous operator id")
    ap.add_argument("--placement", default="", help="how the board is held/placed, e.g. bare-board-handheld")
    ap.add_argument("--surface", default="", help="table surface, e.g. wood / laminate / shelf")
    ap.add_argument("--setup", default="", help="set-up instruction shown before the session starts")
    ap.add_argument("--trials-per-class", type=int, default=2)
    ap.add_argument("--seconds", type=float, default=25.0)
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true", help="simulated board (dry run, writes to data/sim)")
    ap.add_argument("--ui", choices=("console", "board", "terminal"), default=None,
                    help="console: terminal only; board: LCD + board keys; terminal: LCD prompts + typed "
                         "confirmation here (default: board if stdin is not a terminal)")
    ap.add_argument("--labels", default=",".join(LABELS),
                    help="comma-separated labels for this session, e.g. NORMAL_HANDLING (NORMAL-only session)")
    ap.add_argument("--start-delay", type=float, default=0,
                    help="key-free start: show the setup text with a countdown of this many seconds instead of asking for UP")
    a = ap.parse_args()
    labels = tuple(l.strip() for l in a.labels.split(",") if l.strip())
    if not labels or any(l not in LABELS for l in labels):
        sys.exit(f"--labels must be a subset of {LABELS}")

    out_root = os.path.join(ROOT, "data", "sim" if a.sim else "raw")
    sdir = os.path.join(out_root, a.session)
    os.makedirs(sdir, exist_ok=True)
    log_path = os.path.join(sdir, "session_serial.log")
    if a.sim:
        from sim_board import SimBoard
        board = SimBoard(log_path)
    else:
        board = Board(find_port(a.port), log_path)
    ui = a.ui or ("board" if (a.sim or not sys.stdin.isatty()) else "console")
    op = Operator(board, ui, a.sim)
    try:
        ver = board.command("ver", r"^#VER,", timeout=3)
        if not ver:
            sys.exit("board does not answer 'ver' - check the port and that the Smart Item Guardian firmware runs")
        print("board:", ver[2])
        if a.setup and a.start_delay <= 0:
            if not op.ready(f"{a.session}: {a.setup}", timeout=600):
                # Leave an explicit, long-lived status on the LCD so the normal GUARD
                # screen ("No alert") is never mistaken for a finished session.
                if op.lcd:
                    board.send(f"say 600 {a.session} NOT STARTED - no confirmation. Nothing recorded. See PC.")
                sys.exit("operator not ready - session not started (re-run to continue)")
        elif a.start_delay > 0:
            # Key-free start: the operator confirmed placement in chat; count down on the LCD.
            op.tell(f"{a.session} starts in {int(a.start_delay)} s. {a.setup}", a.start_delay)
        op.tell(f"Session {a.session}: put the board down, keep it STILL (gyro calibration)", 3, block=False)
        if not board.command("cal gyro", r"^#CAL,gyro_bias,ok", timeout=20):
            print("WARNING: gyro bias not confirmed; trials will record bias_ok=0")

        order = plan(a.trials_per_class, a.session, labels=labels)
        counters = {lab: 0 for lab in LABELS}
        kept_prev = {lab: 0 for lab in LABELS}
        existing = [f for f in os.listdir(sdir) if f.endswith(".json") and not f.startswith("session")]
        for f in existing:
            lab = f.rsplit("_", 1)[0]
            if lab in counters:
                counters[lab] = max(counters[lab], int(f.rsplit("_", 1)[1][:2]))
                with open(os.path.join(sdir, f)) as fh:
                    if not json.load(fh).get("excluded"):
                        kept_prev[lab] += 1
        # Resume: plan only the trials still missing per label (earlier recordings, kept or
        # excluded, stay untouched on disk).
        for lab in labels:
            for _ in range(min(kept_prev[lab], a.trials_per_class)):
                order.remove(lab)
        if any(kept_prev.values()):
            print(f"resuming {a.session}: already kept {kept_prev}, remaining {len(order)} trial(s)")
        results = []
        i = 0
        redo = 0
        while i < len(order):
            label = order[i]
            n = counters[label] + 1
            print(f"\n=== Trial {i + 1}/{len(order)}  session {a.session}  label {label}  #{n}")
            for line in PROTOCOL[label]:
                print("   " + line)
            action = lcd_action(label, n)
            if label == "NORMAL_HANDLING":
                print(f"   THIS trial's gentle variation: {action}")
            print(f"   Keep doing it from the START beep until the STOP beep ({int(a.seconds)} s).")
            if op.typed:
                op.tell(LCD_WAIT + action, 300, block=False)
                if ask(f"   Trial {i + 1}/{len(order)} {label}: ready? Type y to start (n = pause the session)",
                       ("y", "n")) != "y":
                    print("   paused by the operator before recording")
                    break
                op.tell(f"{action} Starts in 3 s.", 3)
            else:
                # board mode: both hands may be busy with the board, so no key press is needed here
                op.tell(f"Trial {i + 1}/{len(order)} GET READY: {action}", 6)
            op.cue("start")
            if op.lcd:
                board.send(f"say {int(a.seconds)} {Operator._lcd(action + ' STOP at beep')}")
            print(f"   >>> START NOW - {label} - keep going until the stop beep")
            rows, meta, err = record_trial(board, label, a.session, n, a.seconds, a.sim)
            op.cue("stop")
            print("   >>> STOP - put the board down")
            if meta is not None and label == "NORMAL_HANDLING":
                meta["normal_variant"] = NORMAL_VARIANTS[(n - 1) % len(NORMAL_VARIANTS)][0]
            if err:
                print("   ERROR:", err)
                redo += 1
                if redo > MAX_REDO:
                    print("   too many failed attempts - stopping the session")
                    break
                continue
            q = quality(rows)
            print(f"   samples {q['samples']}  rate {q.get('effective_rate_hz')} Hz  missing {q['missing']}  "
                  f"invalid {q['invalid']}  -> {'OK' if q['ok'] else 'POOR'}")
            short = False
            if q["ok"]:
                act = activity(rows, meta.get("gyro_bias", (0.0, 0.0, 0.0)))
                meta["activity"] = act
                if label != "STATIONARY":
                    short = act["active_windows"] < COVERAGE_MIN * act["windows"]
                    print(f"   handling coverage {act['active_windows']}/{act['windows']} windows moving "
                          f"[{act['timeline']}]" + (f"  -> SHORT (< {int(COVERAGE_MIN * 100)} %): automatic redo"
                                                    if short else "  -> OK"))
                    if short:
                        op.tell(f"REDO: moved only {act['active_windows']}/{act['windows']}. Keep moving until beep.", 4)
                else:
                    print(f"   stillness: {act['windows'] - act['active_windows']}/{act['windows']} windows still "
                          f"[{act['timeline']}]" + ("" if act["active_windows"] == 0 else
                                                    "  -> some movement detected: answer n if the board was touched"))
            if short:
                verdict, how, note = "n", "auto_redo_handling_coverage", ""
            elif op.typed and q["ok"]:
                op.tell("Type y/n on PC: done as shown for the whole time?", 60, block=False)
                verdict = op.confirm(f"   Did you do {label} as shown, the WHOLE time until the stop beep? "
                                     "(y keep / n discard+redo / s stop)")
                if verdict == "s":
                    print("   stop requested: this trial is kept for audit but EXCLUDED; session paused")
                note = input("   optional note (Enter to skip): ").strip()
                how = {"y": "confirmed_typed", "n": "rejected_typed", "s": "stopped_typed"}[verdict]
            elif not q["ok"]:
                op.tell("REDO: data gap on the board link. Put the board down.", 3)
                verdict, how, note = "y", "auto_excluded_data_quality", ""
            else:
                # board mode: no key question after a trial. Hardware session 2026-11-18 showed the
                # "press DOWN to redo" prompt was misread (its UP=yes/DN=no footer contradicted it)
                # and discarded 5 clean trials. Trials are kept; the operator reports mistakes
                # afterwards and they are excluded with a recorded reason (never deleted).
                op.tell("Trial saved. Put the board down and rest.", 3)
                verdict = "y"
                how = "kept_operator_followed_lcd (no post-trial key prompt)"
                note = ""
            meta["operator_verdict"] = how
            meta.update({"quality": q, "operator_id": a.operator, "placement": a.placement, "surface": a.surface,
                         "note": note, "excluded": verdict != "y" or not q["ok"],
                         "exclusion_reason": "" if verdict == "y" and q["ok"] else
                         ("data quality" if not q["ok"] else
                          f"handling coverage {meta['activity']['active_windows']}/{meta['activity']['windows']} "
                          f"< {int(COVERAGE_MIN * 100)} % (action not continued until the stop beep)" if short else
                          "operator stopped the session" if verdict == "s" else "operator rejected")})
            base = os.path.join(sdir, meta["trial"])
            if os.path.exists(base + ".csv"):
                base += "_dup"
            with open(base + ".csv", "w", newline="\n") as fh:
                fh.write("seq,t_us,ax,ay,az,gx,gy,gz,flags\n")
                for r in rows:
                    fh.write(",".join(str(v) for v in r) + "\n")
            with open(base + ".json", "w") as fh:
                json.dump(meta, fh, indent=1)
            counters[label] = n
            results.append({"trial": meta["trial"], "label": label, "excluded": meta["excluded"], **q})
            if verdict == "s":
                break
            if meta["excluded"]:
                print(f"   trial kept for audit but EXCLUDED ({meta['exclusion_reason']}); repeating it.")
                redo += 1
                if redo > MAX_REDO:
                    print("   too many repeats of one trial - stopping the session (re-run to continue)")
                    break
                continue
            redo = 0
            i += 1
        complete = i >= len(order)
        if complete:
            op.tell(f"{a.session} COMPLETE: all trials saved. Wait for the next instruction.", 30, block=False)
        else:
            op.tell(f"{a.session} PAUSED - NOT complete. Stop handling. See PC.", 600, block=False)
        summary = {"session": a.session, "labels": list(labels), "complete": complete, "kept_before_this_run": kept_prev, "operator_id": a.operator, "ui": ui, "setup": a.setup, "placement": a.placement, "surface": a.surface,
                   "finished_utc": utc_now(), "board": ver[2], "trials": results, "simulated": a.sim,
                   "kept_per_class": {lab: kept_prev[lab] + sum(1 for r in results if r["label"] == lab and not r["excluded"])
                                      for lab in labels}}
        # session.json marks a COMPLETE session (collect_all/post_capture rely on it);
        # an interrupted run leaves session_partial_<utc>.json and a nonzero exit code.
        name = "session.json" if complete else f"session_partial_{utc_now()[:19].replace(':', '').replace('-', '')}.json"
        with open(os.path.join(sdir, name), "w") as fh:
            json.dump(summary, fh, indent=1)
        print("\nkept trials per class:", summary["kept_per_class"], "" if complete else "(INCOMPLETE - re-run to continue)")
        if not complete:
            sys.exit(2)
        print("expected ~11 windows per 25 s trial (1 s trimmed each end, non-overlapping 2 s windows)")
    finally:
        board.close()


if __name__ == "__main__":
    main()
