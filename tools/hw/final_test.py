"""Hardware session 2 - integrated acceptance run (one organised run, overlapping tests).

  python tools/hw/final_test.py --port COMx            (real board, trained model flashed)
  python tools/hw/final_test.py --sim                  (offline dry run -> evidence/sim/)

Phases (all inside ONE continuous, offline run; the serial log is the record):
  P0 preflight     ver (refuses a placeholder model), RTC time, lat reset, golden self-test on target
  P1 product       20 trials per class in random order, guided; trial-level confusion matrix
  P2 proximity     touchless acknowledgement: 10 deliberate gestures, sustained-near and hands-away checks
  P3 failure       injected IMU read failure: fault alert, responsiveness during the fault, recovery
  P4 diagnostics   all module diagnostics again in the same firmware
  P5 wrap-up       keeps running until >= 15 min total, then lat/cnt/tasks/heap/health
Overlaps: the whole run is offline (no network exists in the product; the operator
also disconnects the PC), so the 5-minute offline requirement is evaluated as the
longest uninterrupted inference stretch; >= 50 timed inferences come from every
window of the run. These trials are never used for training or tuning.
Output: evidence/hardware/final_<UTC>/ serial.log, trials.csv, results.json, summary.md
"""
import argparse
import csv
import datetime
import json
import os
import random
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from analyze_run import analyze, load                                    # noqa: E402
from serial_common import Board, Operator, find_port, host_network_state, parse_kv, utc_now   # noqa: E402

LABELS = ("STATIONARY", "NORMAL_HANDLING", "ROUGH_HANDLING")
SHORT = {"STATIONARY": "STILL", "NORMAL_HANDLING": "NORMAL", "ROUGH_HANDLING": "ROUGH"}
ACTION = {
    "STATIONARY": "put the board down on the table and take your hands off",
    "NORMAL_HANDLING": "hold the board, lift, carry, turn and set it down calmly (no jerks)",
    "ROUGH_HANDLING": "hold FIRMLY with both hands and shake vigorously / jerk-stop (never drop or strike)",
}
LCD_ACTION = {
    "STATIONARY": "HANDS OFF: board on the table.",
    "NORMAL_HANDLING": "NORMAL: lift, carry, set down CALMLY.",
    "ROUGH_HANDLING": "ROUGH: hold FIRMLY, shake + jerk. NEVER drop/hit.",
}
def _load_keymap():
    """firmware key -> PRINTED label, from the gate-2 key check (SIG_KEYMAP=<verify_*.json>).
    Only used to word the instructions; the firmware mapping is not changed."""
    p = os.environ.get("SIG_KEYMAP")
    if not p or not os.path.isfile(p):
        return {}
    with open(p) as fh:
        pairs = json.load(fh).get("printed_to_firmware", {})
    return {fw: printed for printed, fw in pairs.items() if fw in ("UP", "DOWN", "LEFT", "RIGHT")}


KEYMAP = _load_keymap()


def K(fw, lcd=False):
    """How to name firmware key `fw` to the operator (printed word if it differs)."""
    printed = KEYMAP.get(fw)
    if not printed or printed == fw:
        return fw
    return f"key {printed}" if lcd else f"the key PRINTED {printed} (firmware {fw})"


TRIAL_S = 8.0
SETTLE_S = 2.0          # first decisions still contain pre-trial samples (2 s window)


class Run:
    def __init__(self, board, sim, out, op):
        self.b, self.sim, self.out, self.op = board, sim, out, op
        self.res = {"phases": {}, "simulated": sim, "ui": op.ui}

    def wait(self, s):
        if self.sim:
            self.b.sleep(s)
        else:
            time.sleep(s)

    def now(self):
        return self.b.t if self.sim else time.monotonic()

    def enter(self, prompt):
        """Wait for the operator (Enter, or UP on the board in --ui board)."""
        self.op.ready(prompt.strip(), timeout=180)

    def yn(self, prompt):
        return self.op.confirm(prompt, timeout=45)

    def save(self):
        with open(os.path.join(self.out, "results.json"), "w") as fh:
            json.dump(self.res, fh, indent=1)


def infs_between(board, t0, t1):
    out = []
    for mono, _, text in board.lines:
        if t0 <= mono <= t1 and text.startswith("#INF,"):
            f = text.split(",")
            if len(f) > 9:
                out.append({"valid": f[3] == "1", "cls": f[4], "shown": f[7]})
    return out


def events_between(board, t0, t1, name):
    return [(m, t) for m, _, t in board.lines if t0 <= m <= t1 and t.startswith("#EVT,") and f",{name}," in t]


def p0_preflight(r, allow_placeholder):
    b = r.b
    ver = b.command("ver", r"^#VER,", 3)
    if not ver:
        sys.exit("no answer to 'ver'")
    kv = parse_kv(ver[2])
    r.res["firmware"] = kv
    if kv.get("placeholder") == "1" and not allow_placeholder:
        sys.exit("The board runs the PLACEHOLDER model. Flash the trained model first (see docs/hardware_sessions.md).")
    frozen_path = os.path.join(ROOT, "models", "frozen.json")
    if os.path.isfile(frozen_path) and not r.sim:
        with open(frozen_path) as fh:
            frozen_id = json.load(fh)["model_id"]
        r.res["frozen_model_id"] = frozen_id
        if kv.get("model") != frozen_id and not allow_placeholder:
            sys.exit(f"The board runs model {kv.get('model')}, not the frozen model {frozen_id}. Flash the frozen build first.")
    b.command("time set " + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), r"^#DIAG,rtc", 5)
    b.command("lat reset", r"^#LAT,reset", 3)
    b.command("alert test", r"^#EVT,\d+,TEST_ALERT", 3) and b.command("ack", r"^#EVT,\d+,ACK", 3)
    g = b.command("golden", r"^#GOLDEN,SUMMARY", 20)
    net = host_network_state()
    r.res["phases"]["P0"] = {"golden": parse_kv(g[2]) if g else None,
                             "host_adapters_at_start": net,
                             "host_all_adapters_down_at_start": bool(net) and all(x.get("status") != "Up" for x in net),
                             "network_disconnected_by_operator":
                             r.yn("PC network OFF / airplane mode? (the board has no network)") == "y"}
    for cmd, tag in (("sd", "#SDLOG,enabled"), ("ir", "#IR,status"), ("wire", "#WIRE,")):
        x = b.command(cmd, "^" + tag, 6)
        r.res["phases"]["P0"][cmd + "_status_at_start"] = x[2] if x else None
    r.save()


def p1_trials(r, per_class, seed):
    b = r.b
    order = [lab for lab in LABELS for _ in range(per_class)]
    random.Random(seed).shuffle(order)
    trials = []
    m_p1 = b.mark()
    print(f"\nP1: {len(order)} product trials, board held in the hand (no enclosure). Between trials put the board down.")
    for i, label in enumerate(order, 1):
        print(f"\n[{i}/{len(order)}] {label}: {ACTION[label]} for {TRIAL_S:.0f} s")
        if r.op.lcd:
            if r.op.typed:
                r.op.tell(f"Type y on PC. NEXT: {LCD_ACTION[label]}", 300, block=False)
                r.enter(f"   trial {i}/{len(order)} {label}: ready?")
            if i > 1 and (i - 1) % 10 == 0:
                r.op.tell(f"Short rest. Trials {i}-{min(i + 9, len(order))} of {len(order)} follow", 10)
            r.op.tell(f"Trial {i}/{len(order)} GET READY: {LCD_ACTION[label]}", 4)
            r.op.cue("start")
            b.send(f"say {int(TRIAL_S)} {Operator._lcd('NOW: ' + LCD_ACTION[label])}")
        else:
            r.enter("   Enter = start (begin the action immediately)...")
        if r.sim:
            b.activity = label
        b.note(f"trial_start,{i},{label}")
        t0 = r.now()
        r.wait(TRIAL_S)
        t1 = r.now()
        b.note(f"trial_end,{i},{label}")
        r.op.cue("stop")
        if r.sim:
            b.activity = "STATIONARY"
        r.wait(1.0)
        inf = [x for x in infs_between(b, t0 + SETTLE_S, t1 + 0.6) if x["valid"]]
        cnt = {s: sum(1 for x in inf if x["cls"] == s) for s in ("STILL", "NORMAL", "ROUGH")}
        rough_evt = bool(events_between(b, t0, t1 + 1.0, "ROUGH"))
        if cnt["ROUGH"] > 0:
            pred = "ROUGH_HANDLING"
        elif cnt["NORMAL"] > cnt["STILL"]:
            pred = "NORMAL_HANDLING"
        elif inf:
            pred = "STATIONARY"
        else:
            pred = "NO_DECISION"
        rec = {"trial": i, "expected": label, "predicted": pred, "windows": len(inf), **cnt,
               "rough_alert": rough_evt, "correct": pred == label, "start_utc": utc_now()}
        trials.append(rec)
        print(f"   -> predicted {pred}  windows {len(inf)} {cnt}  alert={rough_evt}  {'OK' if rec['correct'] else 'MISS'}")
        if rough_evt:
            if not r.op.lcd:
                print(f"   Press {K('DOWN')} on the board to acknowledge the alert.")
            else:
                print("   (alert acknowledged automatically through the console - no key needed)")
            if r.op.lcd:
                # both hands may be busy with the board: acknowledge through the console (logged)
                r.op.tell("Put the board down and keep it still", 1, block=False)
                b.send("ack")
                b.note("alert_acknowledged_via_console")
            if r.sim:
                b.press_key("DOWN")
            elif not r.op.lcd:
                b.wait_for(r"^#EVT,\d+,ACK", 20)
        r.wait(1.5)
    with open(os.path.join(r.out, "trials.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(trials[0].keys()))
        w.writeheader()
        w.writerows(trials)
    labs = list(LABELS) + ["NO_DECISION"]
    conf = {e: {p: sum(1 for t in trials if t["expected"] == e and t["predicted"] == p) for p in labs} for e in LABELS}
    per = {}
    for c in LABELS:
        tp = conf[c][c]
        fp = sum(conf[e][c] for e in LABELS) - tp
        fn = sum(conf[c].values()) - tp
        per[c] = {"precision": tp / (tp + fp) if tp + fp else 0.0, "recall": tp / (tp + fn) if tp + fn else 0.0,
                  "trials": sum(conf[c].values())}
    false_alerts = sum(1 for t in trials if t["expected"] != "ROUGH_HANDLING" and t["rough_alert"])
    missed_alerts = sum(1 for t in trials if t["expected"] == "ROUGH_HANDLING" and not t["rough_alert"])
    r.res["phases"]["P1"] = {"trials": len(trials), "per_class_trials": {c: per[c]["trials"] for c in LABELS},
                             "accuracy": sum(t["correct"] for t in trials) / len(trials), "confusion": conf,
                             "per_class": per, "false_rough_alerts": false_alerts, "missed_rough_alerts": missed_alerts,
                             # a hand over the proximity sensor while holding the board: counted, never hidden
                             "prox_gestures_during_trials": [t for _, _, t in b.since(m_p1, "#PROX,gesture")]}
    r.save()


def p2_prox(r, cycles):
    """Touchless acknowledgement (AP3216C proximity), replacing the former lid cycles.
    a) deliberate gestures: a TEST alert, then the operator holds a hand over the sensor
       -> must be acknowledged with source=prox (#EVT ACK value 1)
    b) hand kept near: TEST alerts raised while a hand stays over the sensor -> no
       proximity acknowledgement (one-shot / re-arm design)
    c) hands away: TEST alert active, no hand -> no gesture at all
    The board lies on the table; the free hand makes the gestures."""
    b = r.b
    print("\nP2: touchless acknowledgement. Put the board on the table, LCD up; use your free hand.")
    st = b.command("prox", r"^#DIAG,als_ps,", 4)
    cal = parse_kv(st[2]).get("prox_cal") if st else None
    r.res["phases"]["P2_prox_status_at_start"] = st[2] if st else None
    if cal != "measured":
        print("   Proximity is NOT calibrated - running the calibration wizard now (hands away, then hand over sensor).")
        r.op.tell("PROX CAL: hands AWAY now. Hold hand over sensor when LCD says so", 3, block=False)
        m = b.mark()
        b.send("prox cal")
        res_cal = b.wait_for(r"^#CAL,prox,(ok|fail)", 20, m)
        print("   ", res_cal[2] if res_cal else "no calibration result")
        r.res["phases"]["P2_calibration_in_run"] = res_cal[2] if res_cal else None
    res = []
    for k in range(1, cycles + 1):
        if r.op.typed:
            r.op.tell("Type y on PC. Then: hand away, wait for beep, HOLD hand over sensor", 300, block=False)
            r.enter(f"   [{k}/{cycles}] gesture check: keep your hand away now. Ready?")
        if r.sim:
            b.hand(False)
        m = b.mark()
        b.send("alert test")
        raised = b.wait_for(r"^#EVT,\d+,TEST_ALERT,", 3, m)
        r.wait(1.3)                                   # firmware ignores gestures on alerts < 1 s old
        r.op.cue("start")
        r.op.tell(f"[{k}/{cycles}] HOLD HAND over the sensor now (1 s), then take it away", 6, block=False)
        print(f"   [{k}/{cycles}] >>> HOLD your hand over the sensor now (about 1 s), then take it away")
        t0 = r.now()
        if r.sim:
            b.hand(True, hold_s=1.0)
            b.hand(False)
        ack = b.wait_for(r"^#EVT,\d+,ACK,UNKNOWN,1$", 8.0, m)
        lat = (ack[0] - t0) if ack else None
        gestures = [t for _, _, t in b.since(m, "#PROX,gesture")]
        if not ack:
            b.command("ack", r"^#EVT,\d+,ACK", 3)      # clear it through the console for the next cycle
        res.append({"cycle": k, "alert_raised": bool(raised), "acked_by_prox": bool(ack),
                    "cue_to_ack_s": round(lat, 2) if lat is not None else None, "gesture_lines": gestures})
        print(f"      {'ACKNOWLEDGED by gesture in %.2f s' % lat if ack else 'NOT acknowledged by gesture'}  {gestures}")
        r.wait(1.0)
    # b) hand kept near: no acknowledgement while it stays
    if r.op.typed:
        r.op.tell("Type y on PC. Then HOLD hand over sensor and KEEP it there 12 s", 300, block=False)
        r.enter("   SUSTAINED-NEAR check: put your hand over the sensor now and KEEP it there until told. Ready?")
    r.op.cue("start")
    r.op.tell("KEEP your hand over the sensor until the stop beep", 12, block=False)
    print("   >>> keep your hand over the sensor (about 12 s, until the stop beep)")
    m = b.mark()
    if r.sim:
        b.hand(True, hold_s=1.0)
    r.wait(2.0)
    for _ in range(3):
        b.send("alert test")
        r.wait(3.0)
    near_acks = [t for _, _, t in b.since(m) if re.match(r"^#EVT,\d+,ACK,UNKNOWN,1$", t)]
    near_gest = [t for _, _, t in b.since(m, "#PROX,gesture")]
    r.op.cue("stop")
    print(f"      prox acknowledgements while the hand stayed near: {len(near_acks)} (expected 0); "
          f"gesture lines {near_gest}")
    r.op.tell("Take your hand AWAY now", 3, block=False)
    print("   >>> take your hand away")
    if r.sim:
        b.hand(False)
    r.wait(2.0)
    b.command("ack", r"^#EVT,\d+,ACK", 3)
    # c) hands away: no gesture at all
    m = b.mark()
    b.send("alert test")
    r.op.tell("Hands AWAY from the board for 15 s", 15, block=False)
    print("   >>> hands away from the board for 15 s")
    r.wait(15.0)
    away_gest = [t for _, _, t in b.since(m, "#PROX,gesture")]
    b.command("ack", r"^#EVT,\d+,ACK", 3)
    det = [x for x in res if x["acked_by_prox"]]
    r.res["phases"]["P2"] = {
        "gesture_cycles": len(res), "acknowledged_by_gesture": len(det),
        "median_cue_to_ack_s": sorted(x["cue_to_ack_s"] for x in det)[len(det) // 2] if det else None,
        "sustained_near_prox_acks": len(near_acks), "sustained_near_gesture_lines": near_gest,
        "hands_away_gesture_lines": away_gest, "detail": res}
    r.save()


def p3_failure(r, fault_s):
    b = r.b
    print(f"\nP3: controlled failure - IMU reads will be forced to fail for {fault_s:.0f} s.")
    print(f"    During the fault press {K('LEFT')} twice and {K('DOWN')} once on the board.")
    r.op.tell(f"FAULT TEST: board on table. Press {K('LEFT', True)} twice, then {K('DOWN', True)} once", 5)
    m = b.mark()
    t0 = r.now()
    b.send("fault imu on")
    raised = b.wait_for(r"^#FAULT,imu,raised", 3, m)
    status_ok = 0
    end = r.now() + fault_s
    keys_sim = ["LEFT", "LEFT", "DOWN"]
    while r.now() < end:
        if b.command("status", r"^#STATUS,", 2):
            status_ok += 1
        if r.sim and keys_sim:
            b.press_key(keys_sim.pop(0))
        r.wait(2.0)
    during = [x for x in infs_between(b, t0 + 0.5, r.now()) if x["valid"]]
    keys = [t for _, _, t in b.lines[m:] if t.startswith("#KEY,") and ",down," in t]
    t_off = r.now()
    m_off = b.mark()
    b.send("fault imu off")
    cleared = b.wait_for(r"^#FAULT,imu,cleared", 5, m_off)
    resumed = None
    while r.now() < t_off + 8 and resumed is None:
        r.wait(0.5)
        v = [x for x in infs_between(b, t_off, r.now()) if x["valid"]]
        if v:
            resumed = r.now() - t_off
    lcd = r.yn("    Did the LCD show FAULT / SENSOR FAULT and the buzzer sound during the fault?")
    r.res["phases"]["P3"] = {
        "fault_raised": bool(raised), "fault_raise_latency_s": round(raised[0] - t0, 3) if raised else None,
        "status_commands_answered_during_fault": status_ok, "valid_predictions_during_fault": len(during),
        "key_events_during_fault": len(keys), "fault_cleared": bool(cleared),
        "valid_inference_resumed_after_s": round(resumed, 2) if resumed else None, "operator_saw_alert": lcd == "y"}
    r.save()


def p4_diag(r):
    b = r.b
    m = b.mark()
    b.send("diag all")
    b.wait_for(r"^#DIAG,all,DONE", 40, m)
    r.res["phases"]["P4"] = {"diag": [t for _, _, t in b.since(m, "#DIAG,")]}
    r.save()


def p5_wrap(r, t_start, min_minutes):
    b = r.b
    remaining = min_minutes * 60 - (r.now() - t_start)
    if remaining > 0:
        print(f"\nP5: continuing normal operation for {remaining / 60:.1f} more minutes to reach {min_minutes} min.")
        print("    Leave the board on the table; you may handle it occasionally. Do not unplug.")
        while remaining > 0:
            step = min(30, remaining)
            r.wait(step)
            remaining -= step
            if not r.sim:
                print(f"\r    {remaining / 60:4.1f} min left ", end="", flush=True)
        print()
    m = b.mark()
    for cmd in ("lat", "cnt", "tasks", "heap", "status"):
        b.send(cmd)
        r.wait(1.0)
    r.res["phases"]["P5"] = {"wrapup": [t for _, _, t in b.since(m) if t.startswith("#")]}
    net = host_network_state()
    r.res["phases"]["P0"]["host_adapters_at_end"] = net
    r.res["phases"]["P0"]["host_all_adapters_down_at_end"] = bool(net) and all(x.get("status") != "Up" for x in net)
    r.res["phases"]["P0"]["network_still_disconnected_at_end"] = r.yn("Is the PC network still OFF?") == "y"
    for cmd, tag in (("sd", "#SDLOG,enabled"), ("ir", "#IR,status"), ("wire", "#WIRE,")):
        x = b.command(cmd, "^" + tag, 6)
        r.res["phases"]["P5"][cmd + "_status_at_end"] = x[2] if x else None
    r.save()


def longest_inference_stretch(rows, max_gap_s=1.5):
    t = [mono for _, mono, text in rows if text.startswith("#INF,") and text.split(",")[3] == "1"]
    best = cur = 0.0
    for a, b in zip(t, t[1:]):
        cur = cur + (b - a) if b - a <= max_gap_s else 0.0
        best = max(best, cur)
    return best


def summarize(r, t_start, log_path):
    rows = load(log_path)
    run_rows = [x for x in rows if x[1] > t_start]      # strictly after the start marker
    an = analyze(run_rows)
    r.res["analysis"] = an
    P = r.res["phases"]
    dur_min = an.get("host_duration_s", 0) / 60
    stretch = longest_inference_stretch(run_rows) / 60
    inf = an.get("inference_us", {})
    req = []

    def add(rid, text, ok, ev):
        req.append({"id": rid, "requirement": text, "status": "PASS" if ok else "FAIL", "evidence": ev})

    p1 = P.get("P1", {})
    add("R1", ">= 20 independent product trials per class", all(v >= 20 for v in p1.get("per_class_trials", {}).values())
        and len(p1.get("per_class_trials", {})) == 3, f"{p1.get('per_class_trials')} accuracy {p1.get('accuracy', 0):.3f}")
    add("R2", ">= 50 timed on-board inferences (median, p95)", inf.get("n", 0) >= 50,
        f"n={inf.get('n')} median={inf.get('median')} us p95={inf.get('p95')} us")
    s2r = an.get("sample_to_response_us", {})
    s2d = an.get("sample_to_decision_us", {})
    add("R3", "sampling-to-response delay measured", bool(s2r.get("n")),
        f"sample->alert median {s2r.get('median')} us p95 {s2r.get('p95')} us; sample->decision median {s2d.get('median')} us")
    add("R4", ">= 15 min continuous operation, no reset", dur_min >= 15 and an.get("boots_seen", 1) == 0 and an.get("last_stall", 1) == 0,
        f"{dur_min:.1f} min, boots during run {an.get('boots_seen')}, stalls {an.get('last_stall')}, "
        f"missed samples {an.get('last_missed')}, invalid {an.get('last_invalid')}, log drops {an.get('last_log_drop')}")
    add("R5", ">= 5 consecutive minutes offline inference", stretch >= 5 and P.get("P0", {}).get("network_disconnected_by_operator"),
        f"longest uninterrupted inference stretch {stretch:.1f} min; operator network-off attestation "
        f"{P.get('P0', {}).get('network_disconnected_by_operator')}/{P.get('P0', {}).get('network_still_disconnected_at_end')}; "
        f"host adapters all down start/end {P.get('P0', {}).get('host_all_adapters_down_at_start')}/"
        f"{P.get('P0', {}).get('host_all_adapters_down_at_end')}")
    p3 = P.get("P3", {})
    add("R6", "controlled failure with responsive scheduling and user feedback",
        p3.get("fault_raised") and p3.get("fault_cleared") and p3.get("status_commands_answered_during_fault", 0) > 0
        and p3.get("valid_predictions_during_fault", 1) == 0 and p3.get("operator_saw_alert"), json.dumps(p3))
    g = P.get("P0", {}).get("golden") or {}
    add("R7", "exported model executes correctly on STM32 (golden held-out windows)",
        g.get("count") and g.get("python_target_agree") == g.get("count") and g.get("features_bit_exact") == g.get("count"),
        json.dumps(g))
    p2 = P.get("P2", {})
    n2, d2 = p2.get("gesture_cycles", 0), p2.get("acknowledged_by_gesture", 0)
    add("R8", "touchless acknowledgement: >= 90 % of deliberate gestures, none while a hand stays near, none with hands away",
        n2 > 0 and d2 >= 0.9 * n2 and p2.get("sustained_near_prox_acks", 1) == 0 and not p2.get("hands_away_gesture_lines"),
        f"{d2}/{n2} gestures acknowledged (median cue->ack {p2.get('median_cue_to_ack_s')} s); "
        f"prox acks while hand held near {p2.get('sustained_near_prox_acks')}; "
        f"gestures with hands away {len(p2.get('hands_away_gesture_lines') or [])}; "
        f"gestures during P1 handling {p1.get('prox_gestures_during_trials')}")
    diag = P.get("P4", {}).get("diag", [])
    add("R9", "module diagnostics in the same firmware", any("DONE" in d for d in diag),
        f"{sum(',PASS,' in d for d in diag)} PASS / {sum(',FAIL,' in d for d in diag)} FAIL / {sum(',WARN,' in d for d in diag)} WARN")
    add("R10", "heap / stack diagnostics recorded", "heap_min_ever_free" in an,
        f"heap min free {an.get('heap_min_ever_free')} B, stack HWM (words) {an.get('stack_hwm_words_min')}")
    # R11: integrated module functions observed during this run (status lines at start/end)
    def kvs(line):
        return parse_kv(line) if line else {}
    sd0, sd1 = kvs(P.get("P0", {}).get("sd_status_at_start")), kvs(P.get("P5", {}).get("sd_status_at_end"))
    ir1 = kvs(P.get("P5", {}).get("ir_status_at_end"))
    sd_lines = int(sd1.get("lines", 0)) - int(sd0.get("lines", 0)) if sd1 else None
    req.append({"id": "R11", "requirement": "integrated module functions during the run (microSD log, IR relay, wired)",
                "status": "PASS" if (sd_lines or 0) > 0 else "INFO",
                "evidence": f"microSD lines written {sd_lines} (mounted {sd1.get('mounted')}); IR tx_frames "
                            f"{ir1.get('tx_frames')} rx_frames {ir1.get('rx_frames')} relay {ir1.get('relay')}; "
                            f"wire end {P.get('P5', {}).get('wire_status_at_end')}"})
    r.res["requirements"] = req
    r.save()
    title = "Final acceptance run" + (" (SIMULATED - NOT EVIDENCE)" if r.sim else "")
    md = [f"# {title}\n", f"Firmware: {r.res.get('firmware', {}).get('model')} (placeholder={r.res.get('firmware', {}).get('placeholder')})\n",
          f"Set-up: {r.res.get('setup_note', 'not recorded')}\n",
          "| id | requirement | status | evidence |", "|---|---|---|---|"]
    md += [f"| {q['id']} | {q['requirement']} | {q['status']} | {str(q['evidence'])[:220].replace('|', '/')} |" for q in req]
    if p1:
        md += ["\n## Trial-level confusion (rows = expected)\n",
               "| expected | " + " | ".join(SHORT.get(x, x) for x in list(LABELS) + ["NO_DECISION"]) + " |",
               "|---|" + "---|" * 4]
        for e in LABELS:
            md.append(f"| {SHORT[e]} | " + " | ".join(str(p1['confusion'][e][p]) for p in list(LABELS) + ["NO_DECISION"]) + " |")
        md += ["", "| class | precision | recall |", "|---|---|---|"]
        md += [f"| {c} | {v['precision']:.3f} | {v['recall']:.3f} |" for c, v in p1["per_class"].items()]
    with open(os.path.join(r.out, "summary.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(md) + "\n")
    print("\n".join(md))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--ui", choices=("console", "board", "terminal"), default=None,
                    help="console: terminal only; board: LCD + board keys; terminal: LCD prompts/beeps + typed "
                         "confirmations here (default: board if not a terminal)")
    ap.add_argument("--trials-per-class", type=int, default=20)
    ap.add_argument("--prox-cycles", type=int, default=10)
    ap.add_argument("--fault-seconds", type=float, default=20)
    ap.add_argument("--min-minutes", type=float, default=15)
    ap.add_argument("--allow-placeholder", action="store_true", help="plumbing test only; results are not acceptance evidence")
    ap.add_argument("--phases", default="P0,P1,P2,P3,P4,P5")
    a = ap.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = os.path.join(ROOT, "evidence", "sim" if a.sim else "hardware", f"final_{stamp}")
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, "serial.log")
    if a.sim:
        from sim_board import SimBoard
        board = SimBoard(log)
    else:
        board = Board(find_port(a.port), log)
    ui = a.ui or ("board" if (a.sim or not sys.stdin.isatty()) else "console")
    r = Run(board, a.sim, out, Operator(board, ui, a.sim))
    r.res.update({"utc": stamp, "args": vars(a), "keymap_firmware_to_printed": KEYMAP,
                  "keymap_source": os.environ.get("SIG_KEYMAP"),
                  "setup_note": os.environ.get("SIG_SETUP_NOTE", "not recorded")})
    t_start = r.now()
    phases = a.phases.split(",")
    try:
        board.note("final_run_start")
        if "P0" in phases:
            p0_preflight(r, a.allow_placeholder or a.sim)
        if "P1" in phases:
            p1_trials(r, a.trials_per_class, stamp)
        if "P2" in phases:
            p2_prox(r, a.prox_cycles)
        if "P3" in phases:
            p3_failure(r, a.fault_seconds)
        if "P4" in phases:
            p4_diag(r)
        if "P5" in phases:
            p5_wrap(r, t_start, a.min_minutes)
        board.note("final_run_end")
    finally:
        board.close()
    summarize(r, t_start, log)
    print(f"\nwritten: {os.path.relpath(out, ROOT)}")


if __name__ == "__main__":
    main()
