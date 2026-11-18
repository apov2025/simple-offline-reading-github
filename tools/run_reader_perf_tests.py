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
    with tempfile.TemporaryDirectory(prefix="reader-perf-") as temp:
        work = Path(temp)
        # 保留模拟器环境函数，仅重命名窗口程序入口。/ Keep simulator environment functions, renaming only its program entry.
        subprocess.run(["cc", *flags, "-Dmain=sim_program_main", "-c", "src/sim_main.c", "-o", str(work / "sim_main.o")], cwd=SIM, check=True)
        subprocess.run(["cc", *flags, "-include", "idf/sim_fs.h", "-c", "tests/reader_perf_test.c", "-o", str(work / "test.o")], cwd=SIM, check=True)
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
        subprocess.run([str(exe)], cwd=SIM, env={**os.environ, "SIM_DATA": str(work / "data")}, check=True)
        script = work / "boot.txt"
        script.write_text("wait 1200\nquit\n")
        for case in ("fresh", "resume", "missing", "broken"):
            data = work / case
            for directory in ("flash/books", "flash/fonts", "sdcard/books", "sdcard/fonts", "nvs/rp_books"):
                (data / directory).mkdir(parents=True, exist_ok=True)
            book = data / "flash/books/Book.epub"
            with zipfile.ZipFile(book, "w") as archive:
                archive.writestr("mimetype", "application/epub+zip")
                archive.writestr("META-INF/container.xml", '<container><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                archive.writestr("book.opf", '<package><metadata><dc:title>Perf fixture</dc:title></metadata><manifest><item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
                archive.writestr("chapter.xhtml", "<html><body><p>Reading performance fixture.</p></body></html>")
            if case == "broken":
                book.write_bytes(b"broken EPUB")
            if case != "fresh":
                last = "/flash/books/Missing.epub" if case == "missing" else "/flash/books/Book.epub"
                (data / "nvs/rp_books/last").write_bytes(last.encode() + b"\0")
            env = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SIM_FAST": "1", "SIM_LOG": "debug", "SIM_DATA": str(data), "SIM_SCRIPT": str(script)}
            result = subprocess.run([str(SIM / "build/read_pico_sim")], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=True)
            log = result.stdout
            assert "UI ready on" in log, log
            assert log.count("sim_epd: GC16") == (2 if case == "fresh" else 1), log
            if case == "resume":
                assert log.index("reader: opened") < log.index("UI ready on"), log
            else:
                assert "reader: opened" not in log, log
            print(f"reader boot {case}: PASS")



if __name__ == "__main__":
    main()
