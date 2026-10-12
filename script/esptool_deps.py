# PlatformIO pre-script: make sure the Python modules esptool imports are installed.
# esptool runs with PlatformIO's own Python, and its dependencies go missing when
# that Python environment is recreated (e.g. after a PlatformIO update), failing
# the upload with
#   ModuleNotFoundError: No module named 'intelhex'
# Missing modules are installed with pip before the build starts.

Import("env")

import importlib.util
import subprocess

# import name -> pip package
MODULES = {
    "intelhex": "intelhex",
}

if "espressif" in env.subst("$PIOPLATFORM"):
    missing = [
        package for module, package in MODULES.items()
        if importlib.util.find_spec(module) is None
    ]
    if missing:
        print("Installing esptool dependencies: %s" % ", ".join(missing))
        subprocess.check_call([env.subst("$PYTHONEXE"), "-m", "pip", "install"] + missing)
