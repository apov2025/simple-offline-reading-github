#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""用模拟器依赖跑阅读器状态测试，不操作真机或用户数据。
Run reader state tests with simulator dependencies, without touching hardware or user data.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SIM = ROOT / "sim"


def main():
    subprocess.run(["make", "-C", str(SIM), "-j4"], check=True, stdout=subprocess.DEVNULL)
    flags = shlex.split((SIM / "compile_flags.txt").read_text())
    with tempfile.TemporaryDirectory(prefix="reader-metalio-") as temp:
        work = Path(temp)
        # 保留模拟器环境函数，仅重命名窗口程序入口。/ Keep simulator environment functions, renaming only its program entry.
        subprocess.run(["cc", *flags, "-Dmain=sim_program_main", "-c", "src/sim_main.c", "-o", str(work / "sim_main.o")], cwd=SIM, check=True)
        subprocess.run(["cc", *flags, "-include", "idf/sim_fs.h", "-c", "tests/reader_metalio_test.c", "-o", str(work / "test.o")], cwd=SIM, check=True)
        objects = [p for p in (SIM / "build").rglob("*.o") if p.name not in ("app_reader.o", "sim_main.o")]
        libs = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
        if sys.platform == "darwin":
            libs += ["-framework", "Cocoa"]
        exe = work / "test"
        subprocess.run(["cc", *map(str, objects), str(work / "sim_main.o"), str(work / "test.o"), *libs, "-lz", "-lm", "-lpthread", "-o", str(exe)], check=True)
        (work / "data/sdcard/books").mkdir(parents=True)
        (work / "data/flash/books").mkdir(parents=True)
        for folder, name in (("flash", "Fallback.ttf"), ("sdcard", "Saved.ttf")):
            fonts = work / "data" / folder / "fonts"
            fonts.mkdir(parents=True)
            shutil.copy2(ROOT / "builtin/storage/fonts/PicoHei-Regular.ttf", fonts / name)
        (work / "data/flash/fonts" / ("LongFont" + "x" * 180 + ".ttf")).write_bytes(b"invalid")
        subprocess.run([str(exe)], cwd=SIM, env={**os.environ, "SIM_DATA": str(work / "data")}, check=True)


if __name__ == "__main__":
    main()
