"""One clear key verification: printed label vs firmware key name, with a typed cross-check.

  python tools/hw/key_check.py [--port COM3]

For each of UP, DOWN, LEFT, RIGHT:
  1. the LCD says which PRINTED label to press (read the word next to the key; do not
     judge by arrow direction - the keys form an inverted T and the board may be rotated);
  2. the board reports which key the FIRMWARE saw (consumed as a prompt answer: no
     test-alert/ack/screen action is triggered);
  3. this terminal asks you to TYPE the printed label you actually pressed.
Diagnosis per key:
  typed == requested == firmware -> mapping correct
  typed == requested != firmware -> FIRMWARE MAPPING differs from the silkscreen (fix needs a
                                    rebuild + flash = separate approval)
  typed != requested             -> pressed another key; the firmware/typed pair still shows the
                                    mapping of the key that was pressed
No mapping is changed by this tool. Result: evidence/hardware/keymap_check/verify_<UTC>.json/.md
"""
import argparse
import datetime
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from serial_common import Board, ask, find_port, utc_now   # noqa: E402

ORDER = ("UP", "DOWN", "LEFT", "RIGHT")
TYPED = {"u": "UP", "d": "DOWN", "l": "LEFT", "r": "RIGHT", "x": "NONE"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    a = ap.parse_args()
    out = os.path.join(ROOT, "evidence", "hardware", "keymap_check")
    os.makedirs(out, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    b = Board(find_port(a.port), os.path.join(out, f"verify_{stamp}.log"))
    res = []
    try:
        print("Hold the board so you can read the printed words next to the four keys.")
        for i, label in enumerate(ORDER):
            text = f"KEY {i + 1}/4: press ONCE the key PRINTED {label}. Then type on PC."
            assert len(text) <= 80, text
            print(f"\nKEY {i + 1}/4: press the key whose PRINTED word is {label} (once).")
            m = b.mark()
            b.send(f"ask 120 {text}")
            ans = b.wait_for(r"^#ANSWER,", 125, m)
            fw = ans[2].split(",")[2] if ans else "TIMEOUT"
            typed = TYPED[ask("   Which PRINTED word is next to the key you pressed? "
                              "u=UP d=DOWN l=LEFT r=RIGHT x=pressed nothing", tuple(TYPED))]
            if typed == label == fw:
                verdict = "match"
            elif typed == label:
                verdict = "FIRMWARE_MAPPING_DIFFERS"
            elif typed == "NONE":
                verdict = "not_pressed"
            else:
                verdict = "pressed_other_key" + ("_mapping_ok" if typed == fw else "_mapping_differs")
            res.append({"requested_printed": label, "typed_printed": typed, "firmware_key": fw,
                        "verdict": verdict, "utc": utc_now()})
            print(f"   requested {label:5}  you pressed {typed:5}  firmware saw {fw:7} -> {verdict}")
            b.send(f"say 4 You pressed {typed}, firmware saw {fw}.")
            time.sleep(1.5)
        pairs = {r["typed_printed"]: r["firmware_key"] for r in res if r["typed_printed"] != "NONE"}
        all_ok = all(r["verdict"] == "match" for r in res)
        mapping_ok = bool(pairs) and all(k == v for k, v in pairs.items())
        b.send("say 20 " + ("KEY CHECK PASSED: printed labels = firmware" if all_ok else
                            "KEY CHECK: see PC result"))
        time.sleep(1)
    finally:
        b.close()
    summary = {"utc": stamp, "all_match": all_ok, "printed_to_firmware": pairs,
               "mapping_consistent_for_pressed_keys": mapping_ok, "presses": res}
    with open(os.path.join(out, f"verify_{stamp}.json"), "w") as fh:
        json.dump(summary, fh, indent=1)
    md = [f"# Key check {stamp}\n", "| requested (printed) | pressed (typed) | firmware | verdict |", "|---|---|---|---|"]
    md += [f"| {r['requested_printed']} | {r['typed_printed']} | {r['firmware_key']} | {r['verdict']} |" for r in res]
    md += ["", f"printed -> firmware for pressed keys: {pairs}",
           f"result: **{'PASS - mapping correct' if all_ok else ('mapping consistent, operator pressed other keys - repeat' if mapping_ok else 'FIRMWARE MAPPING DIFFERS - remap needs rebuild + flash (separate approval)')}**"]
    with open(os.path.join(out, f"verify_{stamp}.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(md) + "\n")
    print("\n".join(md[-2:]))
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
