"""Proximity (touchless acknowledgement) calibration with recorded sensor evidence.

  python tools/hw/prox_check.py [--port COM3] [--readings 10]

Board on the table, LCD up, USB cable slack. Every step is confirmed by typing here.
  1. raw evidence, hands AWAY: N proximity readings (console 'prox', 5 Hz sensor)
  2. raw evidence, hand held 2-5 cm over the AP3216C light/proximity sensor: N readings
  3. the board's calibration wizard ('prox cal'): hands away, then hand over the sensor;
     it derives thresholds from its own measured statistics (#CAL,prox,...) and stores
     them in RTC backup registers. A failed calibration stores nothing.
No threshold is chosen by this tool. Deliberate-gesture verification (10 gestures,
sustained-near and hands-away checks) is phase P2 of tools/hw/final_test.py.
Result: evidence/hardware/prox_check_<UTC>/result.json + .md. Nothing is flashed or reset.
"""
import argparse
import datetime
import json
import os
import re
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from serial_common import Board, ask, find_port, parse_kv   # noqa: E402


def readings(b, n):
    out = []
    for _ in range(n):
        r = b.command("prox", r"^#DIAG,als_ps,", 4)
        if r:
            kv = parse_kv(r[2])
            out.append({"ps": int(kv.get("ps", -1)), "ps_valid": kv.get("ps_valid") == "1", "als": int(kv.get("als", -1)),
                        "ir": int(kv.get("ir", -1)), "zone": kv.get("prox_zone")})
        time.sleep(0.4)
    return out


def summary(rs):
    ps = [x["ps"] for x in rs if x["ps_valid"]]
    return {"n": len(rs), "valid": len(ps), "min": min(ps) if ps else None, "max": max(ps) if ps else None,
            "median": statistics.median(ps) if ps else None}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--readings", type=int, default=10)
    a = ap.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = os.path.join(ROOT, "evidence", "hardware", f"prox_check_{stamp}")
    os.makedirs(out, exist_ok=True)
    b = Board(find_port(a.port), os.path.join(out, "serial.log"))
    res = {"utc": stamp}
    try:
        print("Find the AP3216C light/proximity sensor on the board (small dark window; check the silkscreen).")
        b.send("say 300 PROX CHECK: board on table, LCD up. Type y on PC.")
        ask("Board on the table, LCD up, cable slack. HANDS AWAY from the board. Ready?", ("y",))
        res["away"] = readings(b, a.readings)
        print(f"   hands away: {summary(res['away'])}")
        b.send("say 300 HOLD a hand 2-5 cm over the sensor, type y with the other hand.")
        ask("HOLD one hand 2-5 cm over the sensor and keep it there; type y with the other hand", ("y",))
        res["near"] = readings(b, a.readings)
        print(f"   hand near:  {summary(res['near'])}")
        b.send("say 30 Take your hand AWAY. Type y on PC.")
        ask("Take your hand away. Ready for the calibration wizard?", ("y",))
        for attempt in (1, 2):
            print("   >>> CALIBRATION: keep hands AWAY for about 6 s. At the tick beep the LCD says "
                  "'HAND OVER SENSOR': hold your hand 2-5 cm over the sensor until the confirmation beep (about 6 s).")
            m = b.mark()
            b.send("prox cal")
            cal = b.wait_for(r"^#CAL,prox,(ok|fail)", 25, m)
            line = cal[2] if cal else "no result within 25 s"
            res[f"calibration_{attempt}"] = line
            print("   " + line)
            if cal and re.match(r"^#CAL,prox,ok", line):
                res["calibration"] = parse_kv(line)
                break
            if attempt == 1 and ask("Calibration failed. Hand closer (2-3 cm), no direct sunlight/lamp into the "
                                    "sensor - repeat once?", ("y", "n")) != "y":
                break
        st = b.command("prox", r"^#DIAG,als_ps,", 4)
        res["status_after"] = st[2] if st else None
        res["pass"] = "calibration" in res
        b.send("say 20 " + ("PROX CAL OK - touchless ack enabled" if res["pass"] else "PROX CAL FAILED - see PC"))
        time.sleep(1)
    finally:
        b.close()
    with open(os.path.join(out, "result.json"), "w") as fh:
        json.dump(res, fh, indent=1)
    c = res.get("calibration", {})
    with open(os.path.join(out, "result.md"), "w", encoding="utf-8") as fh:
        fh.write(f"# Proximity calibration {stamp}\n\n"
                 f"- raw PS, hands away: {summary(res.get('away', []))}\n"
                 f"- raw PS, hand 2-5 cm over the sensor: {summary(res.get('near', []))}\n"
                 f"- board wizard: `{res.get('calibration_2', res.get('calibration_1'))}`\n"
                 f"- thresholds (from the board's measured statistics): far <= {c.get('thr_far')}, near >= "
                 f"{c.get('thr_near')}\n- result: **{'PASS' if res['pass'] else 'FAIL'}**\n")
    print(f"result: {'PASS' if res['pass'] else 'FAIL'} -> {os.path.relpath(out, ROOT)}")
    sys.exit(0 if res["pass"] else 1)


if __name__ == "__main__":
    main()
