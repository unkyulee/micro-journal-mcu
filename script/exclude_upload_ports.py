# PlatformIO pre-script: skip specific serial ports during upload port auto-detection.
# Configure in platformio.ini with:
#   custom_upload_port_exclude = COM17, COM18
# Only USB serial ports are considered. Has no effect when upload_port is set explicitly.

Import("env")

from serial.tools import list_ports

excluded = [
    p.strip().upper()
    for p in env.GetProjectOption("custom_upload_port_exclude", "").replace(",", " ").split()
]

if excluded and not env.subst("$UPLOAD_PORT"):
    candidates = [
        p.device for p in list_ports.comports()
        if p.vid is not None and p.device.upper() not in excluded
    ]
    if candidates:
        env.Replace(UPLOAD_PORT=candidates[0])
        print("Upload port: %s (excluded: %s)" % (candidates[0], ", ".join(excluded)))
    else:
        print("No upload port found after excluding: %s" % ", ".join(excluded))
