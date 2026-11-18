"""Gate-1 status used by the upload guard (pure function, unit-testable offline).

Uploading is allowed only when the LATEST evidence/hardware/gate1_*/gate1.json
(written by tools/hw/gate1_probe.ps1) has status "PASS" and backup_ok true, and the
backup file it names exists with exactly 1,048,576 bytes. A later failed probe
therefore blocks uploads even if an earlier one passed.
"""
import glob
import json
import os

BACKUP_BYTES = 1048576


def check(evidence_hardware_dir):
    runs = sorted(glob.glob(os.path.join(evidence_hardware_dir, "gate1_*", "gate1.json")))
    if not runs:
        return False, "no gate-1 probe/backup record (run tools/hw/gate1_probe.ps1 after approval)"
    latest = runs[-1]
    try:
        with open(latest, encoding="utf-8-sig") as fh:
            g = json.load(fh)
    except (OSError, ValueError) as e:
        return False, f"unreadable {latest}: {e}"
    if g.get("status") != "PASS":
        return False, f"latest gate-1 record is {g.get('status')}: {g.get('reason')} ({latest})"
    if g.get("backup_ok") is not True:
        return False, f"latest gate-1 record has no valid backup ({latest})"
    bin_path = os.path.join(os.path.dirname(latest), g.get("backup_file") or "")
    if not os.path.isfile(bin_path) or os.path.getsize(bin_path) != BACKUP_BYTES:
        return False, f"backup file missing or not {BACKUP_BYTES} bytes: {bin_path}"
    return True, f"gate-1 PASS with backup {os.path.basename(bin_path)}"
