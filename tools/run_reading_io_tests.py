#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实字体/ZIP实现的读盘回归，不访问设备。/ Real font/ZIP I/O regression without devices."""
from pathlib import Path
import subprocess
import tempfile
import zipfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reading-io-") as temp:
    work = Path(temp)
    fixture = work / "empty.zip"
    with zipfile.ZipFile(fixture, "w"):
        pass
    for name, includes, libraries in (
        ("font", ["sim/idf", "main", "main/book", "components/epdiy/src", "components/epdiy/include"], ["-lm"]),
        ("zip", ["tools/zip_host_stubs", "main/book"], ["-lz"]),
    ):
        exe = work / name
        subprocess.run(["cc", "-std=gnu17", "-O1", "-g", "-fsanitize=address,undefined",
                        "-DSIM_HOST=1", *["-I" + str(ROOT / x) for x in includes],
                        str(ROOT / f"tools/{name}_io_host_test.c"), *libraries, "-o", str(exe)], check=True)
        subprocess.run([str(exe), *([str(fixture)] if name == "zip" else [])], check=True)
