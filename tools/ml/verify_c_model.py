"""Host verification: exported C model == trained Python model on held-out windows.

Compiles the firmware's src/core/features.c + src/core/model_infer.c with the
generated src/model/*.h using the MinGW GCC that PlatformIO ships
(toolchain-gccmingw32), with SSE scalar math and -ffp-contract=off so binary32
arithmetic matches the Cortex-M4F FPU, then runs every held-out window and
compares, bit for bit, the 12 features and the predicted class against the
Python mirror. Writes evidence/offline/c_parity_<model>.json/.md.
Exit status 0 only if every window matches exactly.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sigml import CLASSES                     # noqa: E402
from sigml import baseline                    # noqa: E402
from sigml.features import f32                # noqa: E402


def host_gcc():
    """PlatformIO's MinGW gcc; its cc1.exe loads DLLs from the same bin dir, so
    that directory is prepended to PATH for every compiler invocation."""
    mingw_bin = os.path.join(os.path.expanduser("~"), ".platformio", "packages", "toolchain-gccmingw32", "bin")
    if os.path.isdir(mingw_bin):
        os.environ["PATH"] = mingw_bin + os.pathsep + os.environ.get("PATH", "")
    cand = [os.path.join(mingw_bin, "gcc.exe"), "gcc"]
    for c in cand:
        try:
            subprocess.run([c, "--version"], capture_output=True, check=True)
            return c
        except (OSError, subprocess.CalledProcessError):
            continue
    sys.exit("no host gcc found (expected PlatformIO toolchain-gccmingw32)")


HOST_CFLAGS = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-ffp-contract=off", "-fno-math-errno", "-fexcess-precision=standard"]


def build(gcc, out_exe, main_c):
    flags = list(HOST_CFLAGS)
    if "mingw" in gcc.lower() or os.name == "nt":
        flags += ["-msse2", "-mfpmath=sse", "-static"]
    cmd = [gcc] + flags + ["-I", os.path.join(ROOT, "src", "core"), "-I", os.path.join(ROOT, "src", "model"),
                           os.path.join(ROOT, "src", "core", "features.c"),
                           os.path.join(ROOT, "src", "core", "model_infer.c"),
                           main_c, "-o", out_exe, "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("host build failed:\n" + r.stdout + r.stderr)
    return cmd, r.stderr


def bits(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model-dir", help="models/<id> (default: models/latest.json)")
    ap.add_argument("--windows", help="jsonl of windows to check (default: <model-dir>/heldout_windows.jsonl)")
    ap.add_argument("--tag", default="", help="suffix for the evidence file name, e.g. S06")
    ap.add_argument("--windows-from", default="evaluation sessions of train.py (heldout_windows.jsonl)")
    args = ap.parse_args()
    if args.model_dir:
        mdir = args.model_dir
    else:
        with open(os.path.join(ROOT, "models", "latest.json")) as fh:
            mdir = os.path.join(ROOT, json.load(fh)["path"])
    with open(os.path.join(mdir, "model.json")) as fh:
        model = json.load(fh)
    windows = [json.loads(line) for line in open(args.windows or os.path.join(mdir, "heldout_windows.jsonl"))]
    with open(os.path.join(ROOT, "src", "model", "model_tree.h")) as fh:
        if f'"{model["model_id"]}"' not in fh.read():
            sys.exit(f"src/model/model_tree.h is not model {model['model_id']} - re-export before verifying")

    gcc = host_gcc()
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "parity.exe")
        cmd, warn = build(gcc, exe, os.path.join(ROOT, "tests", "host", "parity_main.c"))
        inp, outp = os.path.join(td, "in.txt"), os.path.join(td, "out.txt")
        with open(inp, "w") as fh:
            for w in windows:
                fh.write("%s %d %d %s\n" % (w["id"], w["label"], w["python_pred"],
                                            " ".join("%.9g" % f32(b) for b in w["bias"])))
                for r in w["raw"]:
                    fh.write(" ".join(str(v) for v in r) + "\n")
        r = subprocess.run([exe, inp, outp], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("harness failed: " + r.stderr)
        lines = [ln.split() for ln in open(outp) if not ln.startswith("#")]
        header = [ln for ln in open(outp) if ln.startswith("#")][0].strip()

    bp = model["baseline"]
    results, mismatches = [], []
    for w, ln in zip(windows, lines):
        c_pred, c_base, c_bits = int(ln[1]), int(ln[2]), ln[3:]
        py_bits = [bits(v) for v in w["features"]]
        feat_ok = c_bits == py_bits
        py_base = baseline.predict_one(w["features"], bp)
        ok = feat_ok and c_pred == w["python_pred"] and c_base == py_base
        results.append({"id": w["id"], "label": CLASSES[w["label"]], "python": CLASSES[w["python_pred"]],
                        "c": CLASSES[c_pred] if c_pred < 3 else "INVALID", "features_bit_exact": feat_ok,
                        "baseline_match": c_base == py_base, "ok": ok})
        if not ok:
            mismatches.append({"id": w["id"], "c_bits": c_bits, "py_bits": py_bits, "c_pred": c_pred,
                               "py_pred": w["python_pred"], "c_base": c_base, "py_base": py_base})
    n = len(results)
    summary = {
        "model_id": model["model_id"], "placeholder": model["placeholder"], "windows": n,
        "windows_from": args.windows_from,
        "prediction_agreement": sum(r["c"] == r["python"] for r in results),
        "features_bit_exact": sum(r["features_bit_exact"] for r in results),
        "baseline_agreement": sum(r["baseline_match"] for r in results),
        "all_match": not mismatches and n == len(lines) and n > 0,
        "host_compiler": subprocess.run([gcc, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
        "host_cflags": " ".join(cmd[1:cmd.index("-I")]), "harness_header": header, "compiler_warnings": warn.strip(),
        "mismatches": mismatches[:20],
    }
    os.makedirs(os.path.join(ROOT, "evidence", "offline"), exist_ok=True)
    base = os.path.join(ROOT, "evidence", "offline",
                        f"c_parity_{model['model_id']}" + (f"_{args.tag}" if args.tag else ""))
    with open(base + ".json", "w") as fh:
        json.dump({"summary": summary, "windows": results}, fh, indent=1)
    with open(base + ".md", "w") as fh:
        fh.write(f"# Host C vs Python parity - `{model['model_id']}`\n\n")
        if model["placeholder"]:
            fh.write("> PLACEHOLDER model on SYNTHETIC data: proves the toolchain only.\n\n")
        fh.write(f"- windows: {n} ({summary['windows_from']})\n- class agreement: "
                 f"{summary['prediction_agreement']}/{n}\n- features bit-exact: {summary['features_bit_exact']}/{n}\n"
                 f"- baseline agreement: {summary['baseline_agreement']}/{n}\n- compiler: {summary['host_compiler']}\n"
                 f"- flags: `{summary['host_cflags']}`\n- result: **{'PASS' if summary['all_match'] else 'FAIL'}**\n")
    print(json.dumps({k: v for k, v in summary.items() if k != "mismatches"}, indent=1))
    sys.exit(0 if summary["all_match"] else 1)


if __name__ == "__main__":
    main()
