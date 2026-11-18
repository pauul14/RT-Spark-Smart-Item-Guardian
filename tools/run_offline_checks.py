"""Run every offline check and write a timestamped evidence record.

  1. Python unit tests (tests/python)
  2. host C unit tests of src/core (tests/host/test_core.c, MinGW gcc)
  3. exported-model parity: C (host) vs Python on held-out windows
  4. firmware build (pio run -e rt_spark) with memory report and warning count
  5. firmware footprint: model / golden-vector / section sizes from the ELF
  6. repository hygiene: no CLAUDE.md, no obvious secrets, generated headers current

Output: evidence/offline/offline_checks_<UTC>.md and .json. No hardware is touched.
Usage: python tools/run_offline_checks.py [--skip-build]
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PIO_HOME = os.path.join(os.path.expanduser("~"), ".platformio")
PY = sys.executable
MINGW_BIN = os.path.join(PIO_HOME, "packages", "toolchain-gccmingw32", "bin")
ARM_BIN = os.path.join(PIO_HOME, "packages", "toolchain-gccarmnoneeabi", "bin")
PIO = os.path.join(PIO_HOME, "penv", "Scripts", "pio.exe") if os.name == "nt" else "pio"


def run(cmd, env=None, cwd=ROOT):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def check_python():
    rc, out = run([PY, "-m", "unittest", "discover", "-s", os.path.join("tests", "python"), "-v"])
    m = re.search(r"Ran (\d+) tests", out)
    return {"name": "python unit tests", "pass": rc == 0, "detail": (m.group(0) if m else "") + (" OK" if rc == 0 else " FAILED"),
            "log": out[-4000:]}


def check_host_c():
    env = dict(os.environ, PATH=MINGW_BIN + os.pathsep + os.environ.get("PATH", ""))
    exe = os.path.join(ROOT, ".pio", "host_test_core.exe")
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    srcs = ["tests/host/test_core.c", "src/core/guardian_logic.c", "src/core/prox_ack.c",
            "src/core/latency_stats.c", "src/core/mini_printf.c", "src/core/model_infer.c", "src/core/features.c",
            "src/core/ir_nec_codec.c"]
    cmd = [os.path.join(MINGW_BIN, "gcc.exe"), "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-msse2", "-mfpmath=sse", "-ffp-contract=off", "-fno-math-errno", "-static", "-Isrc/core", "-Isrc/model"] + srcs + ["-o", exe, "-lm"]
    rc, out = run(cmd, env=env)
    if rc != 0:
        return {"name": "host C unit tests", "pass": False, "detail": "compile failed", "log": out[-4000:]}
    warnings = out.count("warning:")
    rc, out2 = run([exe], env=env)
    m = re.search(r"(\d+) passed, (\d+) failed", out2)
    return {"name": "host C unit tests", "pass": rc == 0 and warnings == 0,
            "detail": (m.group(0) if m else out2.strip()) + f", compiler warnings {warnings}", "log": out + out2}


def check_parity():
    rc, out = run([PY, os.path.join("tools", "ml", "verify_c_model.py")])
    try:
        js = json.loads(out[out.index("{"):out.rindex("}") + 1])
        detail = (f"model {js['model_id']} placeholder={js['placeholder']}: classes {js['prediction_agreement']}/{js['windows']}, "
                  f"features bit-exact {js['features_bit_exact']}/{js['windows']}, baseline {js['baseline_agreement']}/{js['windows']}")
    except ValueError:
        detail = "no summary"
    return {"name": "exported C model parity (host)", "pass": rc == 0, "detail": detail, "log": out[-3000:]}


def check_build():
    log_path = os.path.join(ROOT, "evidence", "offline", "build_rt_spark.log")
    elf = os.path.join(ROOT, ".pio", "build", "rt_spark", "firmware.elf")
    if os.path.isfile(elf):
        os.remove(elf)                         # force a relink so the memory report is printed
    rc, out = run([PIO, "run", "-e", "rt_spark"])
    with open(log_path, "w", encoding="utf-8") as fh:
        fh.write(out)
    mem = re.findall(r"^\s+(FLASH|RAM|CCMRAM):\s+(\d+ \w+)\s+(\d+ \w+)\s+([\d.]+%)", out, re.M)
    warnings = len(re.findall(r"warning:", out))
    return {"name": "firmware build (pio run -e rt_spark)", "pass": rc == 0 and warnings == 0,
            "detail": "; ".join(f"{r} {u} / {s} ({p})" for r, u, s, p in mem) + f"; warnings {warnings}",
            "memory": [{"region": r, "used": u, "size": s, "pct": p} for r, u, s, p in mem], "log": out[-2500:]}


def footprint():
    elf = os.path.join(ROOT, ".pio", "build", "rt_spark", "firmware.elf")
    if not os.path.isfile(elf):
        return {"name": "firmware footprint", "pass": False, "detail": "no ELF"}
    rc, out = run([os.path.join(ARM_BIN, "arm-none-eabi-nm.exe"), "-S", "--size-sort", elf])
    sym = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 4:
            sym[p[3]] = int(p[1], 16)
    model = sum(v for k, v in sym.items() if k.startswith("sig_tree_"))
    golden = sum(v for k, v in sym.items() if k.startswith("sig_golden"))
    code_ml = sum(sym.get(k, 0) for k in ("sig_features_compute_ring", "sig_model_predict", "sig_baseline_predict"))
    rc2, sz = run([os.path.join(ARM_BIN, "arm-none-eabi-size.exe"), "-A", elf])
    sections = {p[0]: int(p[1]) for p in (ln.split() for ln in sz.splitlines()) if len(p) >= 2 and p[0].startswith(".") and p[1].isdigit()}
    return {"name": "firmware footprint", "pass": True,
            "detail": f"model tables {model} B, ML code {code_ml} B, golden vectors {golden} B, "
                      f"FreeRTOS heap {sym.get('ucHeap', 0)} B (CCM)",
            "model_table_bytes": model, "ml_code_bytes": code_ml, "golden_bytes": golden,
            "heap_bytes": sym.get("ucHeap", 0), "sections": sections}


def hygiene():
    issues = []
    for dirpath, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in (".pio", ".git", "__pycache__", "reference")]
        for f in files:
            if f.lower() == "claude.md":
                issues.append(os.path.relpath(os.path.join(dirpath, f), ROOT) + " must not exist")
            if f.endswith((".c", ".h", ".py", ".md", ".json", ".ini", ".ps1")):
                p = os.path.join(dirpath, f)
                try:
                    txt = open(p, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                if re.search(r"(?i)(password|passwd|api[_-]?key|secret)\s*[=:]\s*['\"][^'\"]{6,}", txt):
                    issues.append(os.path.relpath(p, ROOT) + " looks like it contains a credential")
                for addr in re.findall(r"[\w.+-]+@[\w-]+\.[A-Za-z][\w.]*", txt):
                    if not addr.lower().startswith("noreply@") and not addr.endswith((".c", ".h", ".py")):
                        issues.append(os.path.relpath(p, ROOT) + " contains an e-mail address (personal data?)")
                        break
    with open(os.path.join(ROOT, "models", "latest.json")) as fh:
        latest = json.load(fh)
    with open(os.path.join(ROOT, "src", "model", "model_tree.h")) as fh:
        if f'"{latest["model_id"]}"' not in fh.read():
            issues.append("src/model headers do not match models/latest.json")
    return {"name": "repository hygiene", "pass": not issues, "detail": "; ".join(issues) or "no issues",
            "model_id": latest["model_id"], "placeholder": latest["placeholder"]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    a = ap.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    results = [check_python(), check_host_c(), check_parity()]
    if not a.skip_build:
        results.append(check_build())
    results += [footprint(), hygiene()]
    ok = all(r["pass"] for r in results)
    out = os.path.join(ROOT, "evidence", "offline")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, f"offline_checks_{stamp}.json"), "w") as fh:
        json.dump({"utc": stamp, "all_pass": ok, "results": results}, fh, indent=1)
    lines = [f"# Offline checks {stamp}\n", f"Overall: **{'PASS' if ok else 'FAIL'}** (no hardware was used)\n",
             "| check | result | detail |", "|---|---|---|"]
    lines += [f"| {r['name']} | {'PASS' if r['pass'] else 'FAIL'} | {r['detail']} |" for r in results]
    with open(os.path.join(out, f"offline_checks_{stamp}.md"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    with open(os.path.join(out, "LATEST.md"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
