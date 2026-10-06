# PlatformIO pre-script: name the build output after the FIRMWARE define.
# An environment with
#   -D FIRMWARE=\"/firmware_rev_8_type1.bin\"
# builds .pio/build/<env>/firmware_rev_8_type1.bin (and .elf / .map) instead of
# firmware.bin - the file name the device looks for when updating.
# Environments without FIRMWARE keep the default firmware.bin.

Import("env")

import re

# the quotes are escaped in platformio.ini, with one or more backslashes
PATTERN = re.compile(r'-D\s*FIRMWARE=\\*"/?([^"\\\s]+)\\*"')


def firmware_names(option):
    flags = env.GetProjectOption(option, "")
    if isinstance(flags, (list, tuple)):
        flags = " ".join(flags)
    return PATTERN.findall(flags)


# an environment that extends another one carries the inherited name too,
# and lists it in build_unflags - the name that is left is the one compiled in
removed = firmware_names("build_unflags")
names = [name for name in firmware_names("build_flags") if name not in removed]

if names:
    name = names[-1]
    if name.lower().endswith(".bin"):
        name = name[:-4]
    env.Replace(PROGNAME=name)
    print("Firmware output: %s.bin" % name)
