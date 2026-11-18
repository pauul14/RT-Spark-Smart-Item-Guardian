"""Compute metrics from a timestamped serial log written by serial_common.Board.

Usage: python tools/hw/analyze_run.py evidence/hardware/<run>/serial.log [--json out.json]

Reports: run duration and resets, inference count, on-board inference time
(cycle counter -> us) median/p95, sample-to-decision and sample-to-response
delays, key-to-feedback latency, heap/stack minima from #HEALTH, missed or
invalid samples, log drops, golden self-test summary, fault-injection timing.
"""
import argparse
import json
import re
import statistics
import sys


def pct(values, p):
    """Nearest-rank percentile (same definition as src/core/latency_stats.c)."""
    if not values:
        return None
    v = sorted(values)
    rank = max(1, -(-p * len(v) // 100))
    return v[int(rank) - 1]


def load(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for ln in fh:
            parts = ln.rstrip("\n").split("\t", 2)
            if len(parts) == 3:
                rows.append((parts[0], float(parts[1]), parts[2]))
    return rows


def kv(text):
    return dict(p.split("=", 1) for p in text.split(",") if "=" in p)


def analyze(rows, cpu_hz=168_000_000):
    out = {"lines": len(rows)}
    fw = [r for r in rows if not r[2].startswith("#HOST")]
    if not fw:
        return out
    out["host_duration_s"] = round(fw[-1][1] - fw[0][1], 1)
    out["boots_seen"] = sum(1 for r in fw if r[2].startswith("#BOOT,fw="))
    out["faults_reported"] = [r[2] for r in fw if r[2].startswith("#FAULT,previous")]
    inf = [r[2].split(",") for r in fw if r[2].startswith("#INF,")]
    valid = [f for f in inf if len(f) > 9 and f[3] == "1"]
    cyc = [int(f[8]) for f in valid]
    s2d = [int(f[9]) for f in valid]
    us = [c / (cpu_hz / 1e6) for c in cyc]
    out["inferences_total"] = len(inf)
    out["inferences_valid"] = len(valid)
    if us:
        out["inference_us"] = {"n": len(us), "median": round(statistics.median(us), 2), "p95": round(pct(us, 95), 2),
                               "min": round(min(us), 2), "max": round(max(us), 2)}
        out["sample_to_decision_us"] = {"n": len(s2d), "median": statistics.median(s2d), "p95": pct(s2d, 95),
                                        "max": max(s2d)}
    resp = [kv(r[2]) for r in fw if r[2].startswith("#RESP,rough")]
    s2r = [int(d["s2r_us"]) for d in resp if "s2r_us" in d]
    s2l = [int(d["s2lcd_us"]) for d in resp if "s2lcd_us" in d]
    if s2r:
        out["sample_to_response_us"] = {"n": len(s2r), "median": statistics.median(s2r), "p95": pct(s2r, 95),
                                        "max": max(s2r), "lcd_banner_median": statistics.median(s2l) if s2l else None}
    keys = [int(kv(r[2])["feedback_us"]) for r in fw if r[2].startswith("#KEY,") and "feedback_us=" in r[2]]
    if keys:
        out["key_to_feedback_us"] = {"n": len(keys), "median": statistics.median(keys), "p95": pct(keys, 95), "max": max(keys)}
    health = [kv(r[2]) for r in fw if r[2].startswith("#HEALTH,")]
    if health:
        last = health[-1]
        out["health_reports"] = len(health)
        out["heap_min_ever_free"] = min(int(h["heap_min"]) for h in health)
        out["stack_hwm_words_min"] = {k[4:]: min(int(h[k]) for h in health) for k in last if k.startswith("hwm_")}
        for k in ("samples", "invalid", "missed", "drdy_to", "qfull", "i2c_err", "i2c_rec", "log_drop", "uart_ovr", "stall", "iwdg"):
            out["last_" + k] = int(last[k]) if k in last else None   # missing = unknown, never 0
    gold = [kv(r[2]) for r in fw if r[2].startswith("#GOLDEN,SUMMARY")]
    if gold:
        out["golden"] = gold[-1]
    evts = [r[2].split(",") for r in fw if r[2].startswith("#EVT,")]
    out["events"] = {}
    for e in evts:
        if len(e) > 2:
            out["events"][e[2]] = out["events"].get(e[2], 0) + 1
    fi = [(r[1], r[2]) for r in rows if "#FAULTINJ,imu" in r[2] or r[2].startswith("#FAULT,imu")]
    if fi:
        out["fault_injection_timeline"] = [{"t": round(t - fw[0][1], 2), "line": l} for t, l in fi]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--json")
    a = ap.parse_args()
    res = analyze(load(a.log))
    txt = json.dumps(res, indent=1)
    if a.json:
        with open(a.json, "w") as fh:
            fh.write(txt)
    print(txt)


if __name__ == "__main__":
    sys.exit(main())
