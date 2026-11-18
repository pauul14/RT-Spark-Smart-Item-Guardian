"""Shared serial access for the hardware-session tools (pyserial 3.5).

- Finds the ST-LINK/V2.1 virtual COM port by USB VID:PID (0483:374B family).
- Opens it with DTR/RTS de-asserted. On the RT-Spark the VCP lines are not
  wired to NRST (schematic sheet 12), so opening the port does not reset the MCU.
- Every received line is written to a log with a host UTC timestamp and a
  monotonic time, so later analysis can align operator actions with firmware output.
The tools only send console text commands; they never flash, erase or reset.
"""
import datetime
import os
import queue
import re
import threading
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:                                   # pragma: no cover
    raise SystemExit("pyserial missing: use PlatformIO's Python (~/.platformio/penv/Scripts/python.exe)")

STLINK_VID = 0x0483
STLINK_PIDS = {0x3748, 0x374B, 0x374E, 0x374F, 0x3752, 0x3753}
BAUD = 115200


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")


def find_port(explicit=None):
    if explicit:
        return explicit
    cands = [p for p in serial.tools.list_ports.comports() if p.vid == STLINK_VID and p.pid in STLINK_PIDS]
    if len(cands) == 1:
        return cands[0].device
    if not cands:
        raise SystemExit("No ST-LINK virtual COM port found. Is the board on the ST-Link USB-C port and powered on?")
    raise SystemExit("Several ST-LINK ports found, pass --port: " + ", ".join(p.device for p in cands))


class Board:
    def __init__(self, port, log_path):
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = BAUD
        self.ser.timeout = 0.1
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.log = open(log_path, "a", encoding="utf-8", newline="\n")
        self.q = queue.Queue()
        self.lines = []                                # (mono, utc, text)
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._reader, daemon=True)
        self._t.start()

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(512)
            except serial.SerialException as e:       # cable pulled / board powered off
                self._record(f"#HOST,serial_error,{e}")
                time.sleep(0.5)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self._record(raw.decode("ascii", errors="replace").rstrip("\r"))

    def _record(self, text):
        item = (time.monotonic(), utc_now(), text)
        self.lines.append(item)
        self.q.put(item)
        self.log.write(f"{item[1]}\t{item[0]:.3f}\t{text}\n")
        self.log.flush()

    def note(self, text):
        """Write an operator/host annotation into the same log."""
        self._record("#HOST," + text)

    def send(self, cmd):
        self.note("send," + cmd)
        self.ser.write((cmd + "\n").encode("ascii"))

    def mark(self):
        return len(self.lines)

    def since(self, mark, prefix=None):
        out = [ln for ln in self.lines[mark:] if not ln[2].startswith("#HOST")]
        return [ln for ln in out if prefix is None or ln[2].startswith(prefix)]

    def wait_for(self, pattern, timeout, mark=None):
        rx = re.compile(pattern)
        start = time.monotonic()
        idx = self.mark() if mark is None else mark
        while time.monotonic() - start < timeout:
            while idx < len(self.lines):
                if rx.search(self.lines[idx][2]):
                    return self.lines[idx]
                idx += 1
            time.sleep(0.05)
        return None

    def command(self, cmd, expect, timeout=5.0):
        m = self.mark()
        self.send(cmd)
        return self.wait_for(expect, timeout, m)

    def close(self):
        self._stop.set()
        self._t.join(timeout=1)
        self.ser.close()
        self.log.close()


def parse_kv(line):
    """'#TAG,a=1,b=x' -> {'a': '1', 'b': 'x'} (positional fields ignored)."""
    out = {}
    for part in line.split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def ask(prompt, choices=("y", "n")):
    while True:
        a = input(f"{prompt} [{'/'.join(choices)}] ").strip().lower()
        if a in choices:
            return a


class Operator:
    """How the tools talk to the person handling the board.

    ui="console": prompts on this terminal (input()), as before.
    ui="board":   prompts on the board's LCD ('say' / 'ask' console commands),
                  answers from the board keys (UP = yes, DOWN = no; any other key
                  or a timeout = no answer), buzzer cues for start/stop. Needs no
                  keyboard, so the agent can run the session while the operator
                  only handles the board. Every prompt is also printed here.
    ui="terminal": LCD prompts and buzzer cues as in "board", but every
                  confirmation is TYPED in this terminal (board keys are not used;
                  added after the 2026-11-18 key-mapping ambiguity).
    """

    def __init__(self, board, ui="console", sim=False):
        self.b, self.ui, self.sim = board, ui, sim
        self.lcd = ui in ("board", "terminal")                     # prompts + cues on the board
        # answers typed on this terminal (SIG_SIM_TYPED=1 lets a --sim dry run exercise the typed path)
        self.typed = ui in ("console", "terminal") and (not sim or os.environ.get("SIG_SIM_TYPED") == "1")

    @staticmethod
    def _lcd(text):
        t = "".join(ch for ch in text if 32 <= ord(ch) < 127)
        return t[:80]

    def wait(self, secs):
        if self.sim:
            self.b.sleep(secs)
        else:
            time.sleep(secs)

    def tell(self, text, secs=5.0, block=True):
        print(f"[operator] {text}")
        if self.lcd:
            self.b.command(f"say {max(1, int(round(secs)))} {self._lcd(text)}", r"^#PROMPT,", 3)
        if block:
            self.wait(secs)

    def cue(self, name):
        if self.lcd:
            self.b.send(f"cue {name}")

    def confirm(self, question, timeout=30):
        """Returns 'y', 'n' or 's' (skipped / no answer)."""
        if self.typed:
            return ask(question, ("y", "n", "s"))
        print(f"[operator] {question}  (board: UP = yes, DOWN = no)")
        m = self.b.mark()
        self.b.send(f"ask {int(timeout)} {self._lcd(question)}")
        ans = self.b.wait_for(r"^#ANSWER,", timeout + 3, m)
        key = ans[2].split(",")[2] if ans else "TIMEOUT"
        print(f"[operator] answer: {key}")
        return {"UP": "y", "DOWN": "n"}.get(key, "s")

    def ready(self, text, timeout=120):
        """Wait until the operator signals ready (Enter, or UP on the board). False on timeout/DOWN."""
        if self.typed:
            return ask(f"{text}\n  Type y when ready (n = do not start)", ("y", "n")) == "y"
        return self.confirm(text + " - UP when ready", timeout) == "y"


def host_network_state():
    """Read-only snapshot of this PC's network adapters (Windows), for the offline-run evidence."""
    import subprocess
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "Get-NetAdapter | Select-Object Name,InterfaceDescription,Status | ConvertTo-Json -Compress"],
                             capture_output=True, text=True, timeout=20).stdout.strip()
        import json as _json
        data = _json.loads(out) if out else []
        if isinstance(data, dict):
            data = [data]
        # statuses only: adapter names can reveal personal software and are not recorded
        return [{"adapter": i + 1, "status": d.get("Status")} for i, d in enumerate(data)]
    except Exception as e:                                   # pragma: no cover
        return [{"error": str(e)}]
