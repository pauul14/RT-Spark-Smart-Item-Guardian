# PlatformIO pre script: refuse `-t upload` unless gate 1 (read-only probe + verified
# 1 MB backup) passed. It only adds a pre-action to the upload target, so builds
# are unaffected. An override exists solely for a separately approved exception and
# must be set explicitly: SIG_GATE1_OVERRIDE=I_HAVE_SEPARATE_APPROVAL
import os
import sys

Import("env")  # noqa: F821

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools", "build"))  # noqa: F821
import gate1_status  # noqa: E402


def _guard(source, target, env):
    if os.environ.get("SIG_GATE1_OVERRIDE") == "I_HAVE_SEPARATE_APPROVAL":
        print("upload_guard: OVERRIDE set - proceeding without a passing gate-1 record")
        return
    ok, msg = gate1_status.check(os.path.join(env.subst("$PROJECT_DIR"), "evidence", "hardware"))
    if not ok:
        sys.stderr.write("\nupload_guard: UPLOAD BLOCKED - " + msg + "\n")
        env.Exit(1)
    print("upload_guard: " + msg)
    # Once a model is frozen (models/frozen.json), only the frozen build may be flashed:
    # every hashed file - including .pio/build/rt_spark/firmware.bin - must be unchanged.
    root = env.subst("$PROJECT_DIR")
    if os.path.isfile(os.path.join(root, "models", "frozen.json")):
        sys.path.insert(0, os.path.join(root, "tools", "ml"))
        import freeze                                   # noqa: E402
        fok, fr, changed = freeze.verify()
        if not fok:
            sys.stderr.write("\nupload_guard: UPLOAD BLOCKED - the build differs from the frozen model "
                             f"{fr['model_id'] if fr else '?'}: " + "; ".join(changed[:5]) + "\n")
            env.Exit(1)
        print(f"upload_guard: frozen build {fr['model_id']} verified (firmware.bin hash matches)")


env.AddPreAction("upload", _guard)  # noqa: F821
