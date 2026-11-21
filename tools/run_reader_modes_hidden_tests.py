#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""在独立临时构建和卡目录中验证阅读模式与隐藏书库。/ Verify reading modes and hidden libraries in an isolated build and card directory."""
from pathlib import Path
import argparse
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview-dir", type=Path, help="Optional directory for rendered screenshots")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="reader-modes-hidden-") as temp:
        work = Path(temp)
        build = work / "build"
        subprocess.run(["make", "-C", str(SIM), "-j4", f"BUILD={build}", str(build / "read_pico_sim")],
                       check=True, stdout=subprocess.DEVNULL)
        # 从 Makefile 取得真实参数，不改共享 compile_flags.txt 或 sim/build。
        # Query actual Makefile flags without changing shared compile_flags.txt or sim/build.
        flag_rules = work / "flags.mk"
        flag_rules.write_text("highlight-test-flags:\n\t@printf '%s\\n' $(CFLAGS) $(SDL_CFLAGS)\n")
        flags = shlex.split(subprocess.check_output(["make", "-s", "-C", str(SIM),
                            f"BUILD={build}", "-f", "Makefile", "-f", str(flag_rules), "highlight-test-flags"], text=True))
        cc = os.environ.get("CC", "cc")
        for source, name, extra in (("src/sim_main.c", "sim_main", ["-Dmain=sim_program_main"]),
                                   ("tests/reader_modes_hidden_test.c", "test", ["-include", "idf/sim_fs.h"])):
            subprocess.run([cc, *flags, *extra, "-c", source, "-o", str(work / f"{name}.o")], cwd=SIM, check=True)
        excluded = {"app_reader.o", "sim_main.o"}
        objects = [p for p in build.rglob("*.o") if p.name not in excluded]
        libs = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
        if sys.platform == "darwin":
            libs += ["-framework", "Cocoa"]
        exe = work / "test"
        subprocess.run([cc, *map(str, objects), str(work / "sim_main.o"), str(work / "test.o"),
                        *libs, "-lz", "-lm", "-lpthread", "-o", str(exe)], check=True)
        data = work / "data"
        for folder in ("flash/fonts", "sdcard/books", "sdcard/notes", "nvs"):
            (data / folder).mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "builtin/storage/fonts/PicoHei-Regular.ttf", data / "flash/fonts/Fallback.ttf")
        for name in ("a", "b"):
            with zipfile.ZipFile(data / "sdcard/books" / (name+".epub"),"w") as z:
                z.writestr("META-INF/container.xml",'<container><rootfiles><rootfile full-path="book.opf"/></rootfiles></container>')
                z.writestr("book.opf",'<package><metadata><title>'+name+'</title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
                z.writestr("body.xhtml",'<html><body><p>First sentence. Second sentence. 第三句话。</p><p>'+('Visible lines for reading. '*100)+'</p></body></html>')
                z.writestr("pico/cover.4bpp",bytes([0x55])*(684*1216//2))
        env = {**os.environ, "SIM_DATA": str(data)}
        if args.preview_dir:
            args.preview_dir.mkdir(parents=True, exist_ok=True)
            env["MODES_PREVIEW"] = str(args.preview_dir.resolve())
        else:
            env.pop("MODES_PREVIEW", None)
        subprocess.run([str(exe)], cwd=SIM, env=env, check=True)


if __name__ == "__main__":
    main()
