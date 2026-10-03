# FEAT-431 B: -flto skal ogsaa med paa LINK-trinnet. PlatformIO/espressif32
# 6.x sender ikke build_flags' -flto videre til linkeren, og saa giver gcc ikke
# LTO-plugin'et til ld ("plugin needed to handle lto object").
Import("env")
env.Append(LINKFLAGS=["-flto"])
