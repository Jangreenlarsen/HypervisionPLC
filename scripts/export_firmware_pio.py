# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# PlatformIO post-script: kopierer firmwaren til projektets rod med version og
# build i navnet, fx HypervisionPLC-v7.9.68.156-b2962.bin — det er den fil, der
# uploades via OTA (web-GUI). Koerer EFTER sign_firmware_pio.py, saa den
# signerede fil bruges, naar den findes; ellers kopieres den usignerede med
# "-unsigned" i navnet (OTA afviser den). Kun den nyeste ligger i roden.
Import("env")
import glob
import os
import re
import shutil

ROOT = env.subst("$PROJECT_DIR")


def _read(path, pattern):
    try:
        with open(path, encoding="utf-8") as f:
            m = re.search(pattern, f.read())
        return m.group(1) if m else None
    except OSError:
        return None


def export_after_build(source, target, env):
    version = _read(os.path.join(ROOT, "include", "constants.h"), r'#define\s+PROJECT_VERSION\s+"([^"]+)"')
    build = _read(os.path.join(ROOT, "include", "build_version.h"), r"#define\s+BUILD_NUMBER\s+(\d+)")
    if not version or not build:
        print("export_firmware: kunne ikke laese PROJECT_VERSION/BUILD_NUMBER — ingen kopi i roden")
        return
    build_dir = env.subst("$BUILD_DIR")
    signed = os.path.join(build_dir, "firmware_signed.bin")
    src, suffix = (signed, "") if os.path.exists(signed) else (os.path.join(build_dir, "firmware.bin"), "-unsigned")
    if not os.path.exists(src):
        print("export_firmware: fandt ingen firmware i " + build_dir)
        return
    for old in glob.glob(os.path.join(ROOT, "HypervisionPLC-v*.bin")):
        os.remove(old)
    out = os.path.join(ROOT, "HypervisionPLC-v%s-b%s%s.bin" % (version, build, suffix))
    shutil.copyfile(src, out)
    print("export_firmware: %s (%d bytes)" % (os.path.basename(out), os.path.getsize(out)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_after_build)
