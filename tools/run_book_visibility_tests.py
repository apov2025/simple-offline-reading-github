#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""隔离验证书籍可见性，不读写设备设置。/ Isolated visibility tests without device settings access."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="book-visibility-") as temp:
    exe = Path(temp) / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", ROOT / "sim/idf", "-I", ROOT / "main",
                    ROOT / "tools/book_visibility_host_test.c", ROOT / "main/book/book_visibility.c",
                    "-o", exe], check=True)
    subprocess.run([exe], check=True)
