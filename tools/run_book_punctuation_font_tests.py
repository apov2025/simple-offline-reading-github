#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实字体标点挤压回归；可追加本机字体路径。/ Real-font punctuation regression; optional extra local font paths."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="book-punctuation-") as directory:
    exe = Path(directory) / "punctuation"
    includes = ["sim/idf", "main", "main/font", "main/book", "components/epdiy/src", "components/epdiy/include"]
    subprocess.run([
        "cc", "-std=gnu17", "-O1", "-g", "-fsanitize=address,undefined", "-DSIM_HOST=1",
        *[f"-I{ROOT / item}" for item in includes],
        str(ROOT / "tools/book_punctuation_font_host_test.c"),
        str(ROOT / "main/book/book_layout.c"), str(ROOT / "main/book/hyphen_en.c"),
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([
        str(exe), str(ROOT / "builtin/storage/fonts/PicoHei-Regular.ttf"),
        *[str(Path(path).resolve()) for path in sys.argv[1:]],
    ], check=True)
