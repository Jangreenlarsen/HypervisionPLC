# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# FEAT-431 B: -flto skal ogsaa med paa LINK-trinnet. PlatformIO/espressif32
# 6.x sender ikke build_flags' -flto videre til linkeren, og saa giver gcc ikke
# LTO-plugin'et til ld ("plugin needed to handle lto object").
Import("env")
env.Append(LINKFLAGS=["-flto"])
