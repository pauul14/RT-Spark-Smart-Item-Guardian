"""Hardware session 1 - bring-up, integrated module functions, evidence.

Start this BEFORE the approved upload so the boot log is captured, in its own terminal:
  terminal A: python tools/hw/bringup.py --wait-boot 180 [--ui board]
  terminal B: (approved, after gate1_probe.ps1 PASS) pio run -e rt_spark -t upload
With --ui board every instruction appears on the board LCD and yes/no answers come
from the board keys (UP = yes, DOWN = no), so no keyboard is needed.

Checks (only console commands are sent; this tool never flashes, erases or resets):
  boot log, clocks, RTOS tasks/heap, every module diagnostic, operator-confirmed
  LCD/matrix/buzzer/LED output, four keys, ALS/PS response to a hand (proximity),
  microSD CSV logging (events really written), IR self-echo + remote pairing/control,
  CAN/RS485 telemetry transmit (partner needed for full verification), 10 s sampling
  interval, golden self-test, latency/counters.
Output: evidence/hardware/bringup_<UTC>/ serial.log, results.json, summary.md
Visual/audible results come from the operator's answers, never assumed.
"""
import argparse
import datetime
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from serial_common import Board, Operator, find_port, parse_kv, utc_now   # noqa: E402

# console diag name -> tag of its result line (src/app/task_health.c)
MODULE_DIAGS = {"imu": "imu", "als": "als_ps", "aht": "aht21", "flash": "spi_flash", "sd": "sdcard", "rtc": "rtc",
                "wifi": "wifi_rw007", "codec": "codec_es8388", "can": "can1", "mcu": "mcu_adc", "i2cscan": "i2c2_scan"}


class Session:
    def __init__(self, board, op):
        self.b, self.op = board, op
        self.results = []

    def rec(self, item, status, evidence, note=""):
        self.results.append({"item": item, "status": status, "evidence": evidence, "note": note, "utc": utc_now()})
        print(f"  [{status:7}] {item}: {evidence[:120]}")

    def answer(self, q, timeout=30):
        return {"y": "PASS", "n": "FAIL", "s": "NO-ANSWER"}[self.op.confirm(q, timeout)]

    def lines(self, cmd, prefix, wait=1.5):
        m = self.b.mark()
        self.b.send(cmd)
        self.op.wait(wait)
        return [ln[2] for ln in self.b.since(m, prefix)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--wait-boot", type=float, default=0, help="seconds to wait for a #BOOT line first")
    ap.add_argument("--ui", choices=("console", "board"), default=None)
    ap.add_argument("--sim", action="store_true")
    a = ap.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = os.path.join(ROOT, "evidence", "sim" if a.sim else "hardware", f"bringup_{stamp}")
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, "serial.log")
    if a.sim:
        from sim_board import SimBoard
        board = SimBoard(log)
    else:
        board = Board(find_port(a.port), log)
    ui = a.ui or ("board" if (a.sim or not sys.stdin.isatty()) else "console")
    op = Operator(board, ui, a.sim)
    s = Session(board, op)
    try:
        # ---------------- boot ----------------
        if a.wait_boot and not a.sim:
            print(f"Waiting up to {a.wait_boot:.0f} s for #BOOT (start the approved upload in the other terminal now)...")
            boot = board.wait_for(r"^#BOOT,fw=", a.wait_boot, 0)
            s.rec("boot banner / reset cause", "PASS" if boot else "MISSING", boot[2] if boot else "no #BOOT seen")
            board.wait_for(r"^#RTOS,scheduler_running", 15, 0)
        for tag in ("#BOOT,fw=", "#CLOCK,", "#INIT,", "#RTOS,", "#FAULT,previous"):
            for ln in board.since(0, tag):
                s.rec("boot log " + tag.strip("#,="), "INFO", ln[2])
        ver = board.command("ver", r"^#VER,", 3)
        s.rec("console + firmware identity", "PASS" if ver else "FAIL", ver[2] if ver else "no answer to 'ver'")
        if not ver:
            sys.exit("No console response; stop here and check port/power (docs/hardware_sessions.md).")
        board.command("time set " + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), r"^#DIAG,rtc", 5)
        for ln in s.lines("tasks", "#TASK,") + s.lines("heap", "#HEAP,", 0.5):
            s.rec("FreeRTOS task/heap", "INFO", ln)

        # ---------------- read-only module diagnostics (batched, board still) ----------------
        op.tell("Bring-up: leave the board on the table, do not touch it for 30 s", 2, block=False)
        for d, tag in MODULE_DIAGS.items():
            r = board.command(f"diag {d}", rf"^#DIAG,{tag},", 12)
            if r:
                parts = r[2].split(",")
                st = parts[2] if len(parts) > 2 and parts[2] in ("PASS", "FAIL", "WARN", "N/A") else "INFO"
                s.rec(f"diag {d}", st, r[2])
            else:
                s.rec(f"diag {d}", "TIMEOUT", "no #DIAG line")
            op.wait(0.3)

        # ---------------- sampling interval (still) ----------------
        m = board.mark()
        board.command("cap start STATIONARY BRINGUP 0", r"^#CAP,START", 3)
        op.wait(10)
        board.command("cap stop", r"^#CAP,STOP", 3)
        rows = [ln[2].split(",") for ln in board.since(m, "S,")]
        if len(rows) > 10:
            t = [int(r[2]) for r in rows]
            d = sorted((y - x) & 0xFFFFFFFF for x, y in zip(t, t[1:]))
            seq = [int(r[1]) for r in rows]
            miss = sum(max(0, y - x - 1) for x, y in zip(seq, seq[1:]))
            med = d[len(d) // 2]
            s.rec("IMU sampling interval", "PASS" if 9500 <= med <= 10500 and miss == 0 else "WARN",
                  f"n={len(rows)} median_dt_us={med} min={d[0]} max={d[-1]} missing={miss} "
                  f"polled_flag={sum(1 for r in rows if int(r[9]) & 4)}")
        else:
            s.rec("IMU sampling interval", "FAIL", f"only {len(rows)} samples")

        # ---------------- outputs confirmed by the operator ----------------
        checks = [("lcd", r"^#DIAG,lcd,DONE", "Did the LCD show red, green, blue, white, black bars?"),
                  ("matrix", r"^#DIAG,matrix,DONE", "Did all 19 matrix LEDs show R,G,B,W then a green chase?"),
                  ("buzzer", r"^#DIAG,buzzer,DONE", "Did the buzzer give 3 short beeps?"),
                  ("leds", r"^#DIAG,leds,DONE", "Did the red and second LED alternate 3 times?")]
        for name, pat, q in checks:
            r = board.command(f"diag {name}", pat, 10)
            s.rec(f"{name} operator check", s.answer(q), r[2] if r else "no DONE line")

        # ---------------- keys ----------------
        op.tell("Press UP, DOWN, LEFT, RIGHT once each (short presses)", 20, block=False)
        m = board.mark()
        if a.sim:
            for k in ("UP", "DOWN", "LEFT", "RIGHT"):
                board.press_key(k)
        t_end = time.monotonic() + 25
        while not a.sim and time.monotonic() < t_end:
            seen = {ln[2].split(",")[1] for ln in board.since(m, "#KEY,") if ",down," in ln[2]}
            if {"UP", "DOWN", "LEFT", "RIGHT"} <= seen:
                break
            time.sleep(0.3)
        seen = {ln[2].split(",")[1]: ln[2] for ln in board.since(m, "#KEY,") if ",down," in ln[2]}
        for k in ("UP", "DOWN", "LEFT", "RIGHT"):
            s.rec(f"key {k}", "PASS" if k in seen else "FAIL", seen.get(k, "no #KEY line within 25 s"))
        board.send("ack")
        board.send("screen 0")
        op.wait(1)

        # ---------------- AP3216C response to a hand (proximity) ----------------
        vals = []
        for step, txt in (("uncovered", "Keep your hands AWAY from the light/proximity sensor"),
                          ("covered", "HOLD a hand 2-5 cm over the light/proximity sensor"),
                          ("uncovered again", "Take your hand AWAY")):
            op.tell(txt, 7)
            if a.sim:
                board.hand(step == "covered", hold_s=0.2)   # simulated hand; too short for a gesture
            r = board.command("prox", r"^#DIAG,als_ps", 5)
            vals.append(r[2] if r else "no reading")
            s.rec(f"ALS/PS {step}", "INFO", vals[-1])
        try:
            k = [parse_kv(v) for v in vals]
            ps = [int(x.get("ps", 0)) for x in k]
            als = [int(x.get("als", 0)) for x in k]
            resp = (ps[1] - max(ps[0], ps[2]) >= 12) or (min(als[0], als[2]) - als[1] >= 12)
            s.rec("AP3216C responds to cover (PS up or ALS down by >= 12 counts)", "PASS" if resp else "FAIL",
                  f"ps={ps} als={als}")
        except (ValueError, IndexError):
            s.rec("AP3216C responds to cover", "FAIL", "could not parse readings")
        s.rec("proximity calibration", "PENDING", "measured in the final session with tools/hw/prox_check.py")

        # ---------------- microSD product log ----------------
        sd0 = board.command("sd", r"^#SDLOG,enabled", 8)
        k0 = parse_kv(sd0[2]) if sd0 else {}
        if k0.get("mounted") == "1":
            board.send("alert test")
            op.wait(1)
            board.send("ack")
            op.wait(2.5)
            sd1 = board.command("sd", r"^#SDLOG,enabled", 8)
            k1 = parse_kv(sd1[2]) if sd1 else {}
            grew = int(k1.get("lines", 0)) > int(k0.get("lines", 0))
            s.rec("microSD event log (TEST_ALERT/ACK written to SIGUARD/EVENTS.CSV)", "PASS" if grew else "FAIL",
                  f"before {sd0[2]} | after {sd1[2] if sd1 else 'none'}")
        else:
            s.rec("microSD event log", "PENDING", (sd0[2] if sd0 else "no #SDLOG") +
                  " - insert a FAT32 microSD card to verify (it is mounted within 30 s)")

        # ---------------- IR: self echo, remote pairing and control ----------------
        op.tell("IR: hold a white card ~5 cm above the IR LED and receiver", 4)
        r = board.command("diag ir", r"^#DIAG,ir", 15)
        s.rec("IR emitter -> receiver decoded self-echo", (r[2].split(",")[2] if r else "TIMEOUT"), r[2] if r else "none")
        if op.confirm("Do you have any TV/IR remote control? UP=yes", 45) == "y":
            m = board.mark()
            board.send("ir learn")
            done = board.wait_for(r"^#IR,pairing,done", 60, m)
            paired = [ln[2] for ln in board.since(m, "#IR,paired")]
            s.rec("IR remote pairing", "PASS" if done and paired else "FAIL", "; ".join(paired) or "nothing paired")
            if paired and any(",TEST ALERT," in p for p in paired):
                m_ir = board.mark()                          # before the prompt: the press may come quickly
                op.tell("Press the remote key you paired for TEST ALERT", 10, block=False)
                hit = board.wait_for(r"^#IR,rx,.*action=TEST ALERT", 20, m_ir)
                s.rec("IR remote control (paired key raises a TEST alert)", "PASS" if hit else "FAIL",
                      hit[2] if hit else "no TEST ALERT action")
                board.send("ack")
        else:
            s.rec("IR remote pairing/control", "PENDING", "no remote available")

        # ---------------- wired telemetry (transmit side; partner needed for receive) ----------------
        for bus in ("can", "rs485"):
            board.send(f"wire {bus} on")
            op.wait(6.5)                                     # at least one 5 s heartbeat
            w = board.command("wire", r"^#WIRE,", 5)
            board.send(f"wire {bus} off")
            k = parse_kv(w[2]) if w else {}
            if bus == "can":
                tx = int(k.get("can_tx_ok", 0)) + int(k.get("can_tx_fail", 0))
                st = "PASS" if int(k.get("can_tx_ok", 0)) > 0 else ("PARTIAL" if tx > 0 else "FAIL")
                note = "acknowledged by a partner" if st == "PASS" else "frames sent but not acknowledged: no bus partner (expected without a second node)"
            else:
                st = "PARTIAL" if int(k.get("rs485_lines", 0)) > 0 else "FAIL"
                note = "lines transmitted; reception needs a partner/USB-RS485 adapter"
            s.rec(f"{bus.upper()} telemetry", st, w[2] if w else "no #WIRE", note)
            op.wait(0.5)

        # ---------------- model + counters ----------------
        g = board.command("golden", r"^#GOLDEN,SUMMARY", 20)
        s.rec("on-target model self-test (golden)", "INFO", g[2] if g else "no summary")
        for ln in s.lines("lat", "#LAT,") + s.lines("cnt", "#CNT,") + s.lines("ir", "#IR,status"):
            s.rec("counters", "INFO", ln)
        op.tell("Bring-up complete", 3, block=False)
    finally:
        board.close()
        with open(os.path.join(out, "results.json"), "w") as fh:
            json.dump({"utc": stamp, "simulated": a.sim, "ui": ui, "results": s.results}, fh, indent=1)
        lines = [f"# Bring-up {stamp}{' (SIMULATED - not evidence)' if a.sim else ''}\n",
                 "| item | status | evidence | note |", "|---|---|---|---|"]
        lines += [f"| {r['item']} | {r['status']} | {r['evidence'][:160].replace('|', '/')} | {r['note']} |"
                  for r in s.results]
        with open(os.path.join(out, "summary.md"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
        print(f"\nwritten: {os.path.relpath(out, ROOT)}")


if __name__ == "__main__":
    main()
