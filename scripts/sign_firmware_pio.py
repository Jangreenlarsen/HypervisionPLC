# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# FEAT-439: PlatformIO post-script — signerer firmware.bin til firmware_signed.bin
# efter hvert build, hvis den private noegle (certs/ota_signing.key) findes.
# Koerer scripts/sign_firmware.py med systemets python (kraever pakken
# "cryptography"); mangler noeglen eller pakken, advares der kun.
Import("env")
import os
import subprocess

ROOT = env.subst("$PROJECT_DIR")
KEY = os.path.join(ROOT, "certs", "ota_signing.key")


def sign_after_build(source, target, env):
    fw = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    if not os.path.exists(KEY):
        print("FEAT-439: ingen certs/ota_signing.key — firmware_signed.bin IKKE lavet (OTA kraever signeret fil)")
        return
    out = fw.replace("firmware.bin", "firmware_signed.bin")
    if os.path.exists(out):
        os.remove(out)
    # PIO saetter sin egen python forrest i PATH (uden "cryptography") — proev
    # hver python paa PATH, og spring videre ved manglende modul
    seen = set()
    for d in os.environ.get("PATH", "").split(os.pathsep):
        for name in ("python.exe", "python3", "python"):
            py = os.path.join(d, name)
            if not os.path.isfile(py) or os.path.normcase(os.path.realpath(py)) in seen:
                continue
            seen.add(os.path.normcase(os.path.realpath(py)))
            try:
                r = subprocess.run([py, os.path.join(ROOT, "scripts", "sign_firmware.py"), fw, out],
                                   capture_output=True, text=True, timeout=60)
            except (OSError, subprocess.TimeoutExpired):
                continue
            if "No module named" in r.stderr:
                continue
            print(r.stdout.strip() or r.stderr.strip())
            return
    print("FEAT-439: fandt ingen python med 'cryptography' — signér manuelt med scripts/sign_firmware.py")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign_after_build)
