"""Final hardware session, operator part, in ONE local terminal run (works with the PC
offline; no Claude connection needed). Handheld prototype: no enclosure, no lid.

  python tools/hw/gate2_run.py [--port COM3]

  0. prerequisites: frozen artifacts intact (tools/ml/freeze.py --verify) and the board
     reports the frozen model (placeholder=0) and the frozen firmware build - else STOP
  1. button check   tools/hw/key_check.py   (typed cross-check; no mapping change)
  2. proximity      tools/hw/prox_check.py  (raw readings + measured calibration -> RTC backup registers)
  3. network OFF    you switch the PC to airplane mode; adapter statuses are recorded (read-only)
  4. final test     tools/hw/final_test.py --ui terminal  (~35-45 min, fully offline)
  5. done           all serial tools closed; you may reconnect the network

Steps 1-2 results are recorded, never waived; a failed check does not block the final
test (it stays listed as unresolved). Run it once. Output: evidence/hardware/gate2_<UTC>/
No upload, reset or power action is performed here.
"""
import argparse
import datetime
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "ml"))
from serial_common import Board, ask, find_port, host_network_state, parse_kv   # noqa: E402

PY = sys.executable
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
OUT = os.path.join(ROOT, "evidence", "hardware", f"gate2_{STAMP}")
LOG = os.path.join(OUT, "runner.log")
SETUP = "handheld prototype: bare RT-Spark board, no enclosure, not mounted to an item"


def log(text):
    sys.stdout.write(text)
    sys.stdout.flush()
    with open(LOG, "a", encoding="utf-8") as fh:
        fh.write(text)


def banner(text):
    log(f"\n{'#' * 70}\n# {text}\n{'#' * 70}\n")


def run(args, env=None):
    p = subprocess.Popen([PY, "-u"] + args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         env=dict(os.environ, **(env or {})))
    fd = p.stdout.fileno()
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        log(chunk.decode("utf-8", "replace").replace("\r\n", "\n"))
    return p.wait()


def newest(pattern, after):
    c = [p for p in glob.glob(pattern) if os.path.getmtime(p) >= after]
    return max(c, key=os.path.getmtime) if c else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM3")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    res = {"utc": STAMP, "setup": SETUP, "steps": {}}

    def save():
        with open(os.path.join(OUT, "runner.json"), "w") as fh:
            json.dump(res, fh, indent=1)

    banner("STEP 0/5  prerequisites: frozen artifacts + board identity")
    import freeze as FZ
    ok, fr, changed = FZ.verify()
    if not ok:
        log("STOP: frozen artifacts changed: " + "; ".join(changed) + "\n")
        res["steps"]["prereq"] = "STOP frozen artifacts changed"
        save()
        sys.exit(1)
    b = Board(find_port(a.port), os.path.join(OUT, "prereq_serial.log"))
    ver = b.command("ver", r"^#VER,", 4)
    b.close()
    kv = parse_kv(ver[2]) if ver else {}
    stamps = fr.get("firmware_built_stamps") or []
    log(f"frozen model {fr['model_id']}, firmware build stamps {stamps or '(not recorded)'}; "
        f"board: {ver[2] if ver else 'NO ANSWER'}\n")
    if (not ver or kv.get("model") != fr["model_id"] or kv.get("placeholder") != "0"
            or (stamps and kv.get("built") not in stamps)):
        log("STOP: the board does not report the frozen model/firmware with placeholder=0 - nothing else is run.\n")
        res["steps"]["prereq"] = f"STOP identity mismatch: {ver[2] if ver else 'no answer'}"
        save()
        sys.exit(1)
    res["steps"]["prereq"] = "ok"
    res["board"] = ver[2]
    save()

    banner("STEP 1/5  button check (board in front of you, printed key words readable)")
    t1 = os.path.getmtime(LOG)
    keymap = None
    for attempt in (1, 2):
        rc = run([os.path.join("tools", "hw", "key_check.py"), "--port", a.port])
        keymap = newest(os.path.join(ROOT, "evidence", "hardware", "keymap_check", "verify_*.json"), t1)
        verdict = None
        if keymap:
            with open(keymap) as fh:
                kd = json.load(fh)
            verdict = ("PASS" if kd["all_match"] else
                       "mapping consistent, operator pressed other keys" if kd["mapping_consistent_for_pressed_keys"]
                       else "FIRMWARE MAPPING DIFFERS")
        res["steps"][f"key_check_{attempt}"] = {"rc": rc, "file": keymap and os.path.relpath(keymap, ROOT),
                                                "verdict": verdict}
        save()
        if rc == 0 or attempt == 2 or ask("Repeat the button check once?", ("y", "n")) != "y":
            break
    if keymap:
        with open(keymap) as fh:
            pairs = json.load(fh)["printed_to_firmware"]
        log("\nKEY CHEAT SHEET for the final test (firmware name -> key with this PRINTED word):\n")
        for fw in ("UP", "DOWN", "LEFT", "RIGHT"):
            printed = [p for p, f in pairs.items() if f == fw]
            log(f"   firmware {fw:5} -> printed {printed[0] if printed else '(not identified)'}\n")
        log("The final test names the PRINTED key wherever it differs.\n")

    banner("STEP 2/5  proximity calibration (touchless acknowledgement)")
    t2 = os.path.getmtime(LOG)
    rc = run([os.path.join("tools", "hw", "prox_check.py"), "--port", a.port])
    pdir = newest(os.path.join(ROOT, "evidence", "hardware", "prox_check_*", "result.json"), t2)
    res["steps"]["prox_check"] = {"rc": rc, "dir": pdir and os.path.relpath(os.path.dirname(pdir), ROOT)}
    save()
    log(f"\nproximity calibration: {'PASS' if rc == 0 else 'FAIL (recorded as unresolved; the final test retries once)'}\n")

    banner("STEP 3/5  network OFF")
    log("Now switch this PC to AIRPLANE MODE (or unplug the network cable and turn Wi-Fi off).\n"
        "The Claude connection will drop - that is expected. Everything from here runs locally.\n"
        "Keep this terminal open. Do not close VS Code.\n")
    while True:
        ask("Network is OFF?", ("y",))
        net = host_network_state()
        up = [n for n in net if str(n.get("status")).lower() == "up"]
        log(f"adapter statuses: {[n.get('status') for n in net]}\n")
        if not up:
            break
        if ask(f"{len(up)} adapter(s) still report Up (can be a virtual/USB adapter). Continue anyway?",
               ("y", "n")) == "y":
            break
    res["steps"]["network_before_final"] = net
    save()

    banner("STEP 4/5  FINAL TEST (~35-45 min, offline). Follow the LCD and type here.")
    log("Handheld: STILL = board on the table, hands off; NORMAL = hold it, lift/carry/turn calmly;\n"
        "ROUGH = hold FIRMLY with both hands, shake + jerk, NEVER drop or hit it. Keep the USB cable slack.\n"
        "Proximity checks: board on the table, use your free hand over the sensor as the terminal says.\n"
        "In the fault test, press the keys the terminal names.\n")
    t4 = os.path.getmtime(LOG)
    env = {"SIG_SETUP_NOTE": SETUP}
    if keymap:
        env["SIG_KEYMAP"] = keymap
    rc = run([os.path.join("tools", "hw", "final_test.py"), "--port", a.port, "--ui", "terminal"], env=env)
    final_dir = newest(os.path.join(ROOT, "evidence", "hardware", "final_*", "summary.md"), t4)
    res["steps"]["final_test"] = {"rc": rc, "dir": final_dir and os.path.relpath(os.path.dirname(final_dir), ROOT)}
    save()

    banner("STEP 5/5  DONE - all tools have closed the serial port")
    log(f"final test exit code {rc}; evidence: {res['steps']['final_test']['dir']}\n"
        "You may now RECONNECT the network (turn airplane mode off) and tell the assistant 'final session done'.\n"
        "Leave the board powered until the assistant confirms the evidence is complete.\n")
    log(f"runner log: {os.path.relpath(LOG, ROOT)}\n")


if __name__ == "__main__":
    main()
