#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""用硬件替身测试真实睡眠循环。/ Test both real wallpaper display entry points with hardware stand-ins."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

SIM = Path(__file__).resolve().parents[1] / "sim"

def main():
    subprocess.run(["make", "-C", str(SIM), "-j4"], check=True, stdout=subprocess.DEVNULL)
    flags = shlex.split((SIM / "compile_flags.txt").read_text())
    with tempfile.TemporaryDirectory(prefix="sleep-wake-") as temp:
        work = Path(temp)
        subprocess.run(["cc", *flags, "-Dmain=sim_program_main", "-c", "src/sim_main.c", "-o", str(work / "sim_main.o")], cwd=SIM, check=True)
        subprocess.run(["cc", *flags, "-include", "idf/sim_fs.h", "-c", "tests/wallpaper_power_test.c", "-o", str(work / "test.o")], cwd=SIM, check=True)
        objects = [p for p in (SIM / "build").rglob("*.o") if p.name not in ("sleep.o", "app_sleep.o", "sim_main.o")]
        libs = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
        if sys.platform == "darwin":
            libs += ["-framework", "Cocoa"]
        exe = work / "test"
        subprocess.run(["cc", *map(str, objects), str(work / "sim_main.o"), str(work / "test.o"), *libs, "-lz", "-lm", "-lpthread", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], env={**os.environ, "SIM_DATA": str(work / "data")}, timeout=30, check=True)

if __name__ == "__main__":
    main()
