#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""临时目录内验证划线存储，不访问真实卡。/ Exercise highlight storage without accessing a real card."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reader-highlights-") as temp:
    work = Path(temp)
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    exe = work / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f'-DREADER_HIGHLIGHT_DIRECTORY="{work / "notes/highlights"}"',
                    "-I", ROOT / "sim/idf", "-I", ROOT / "main", "-I", ROOT / "components/read_pico/include", "-I", cjson,
                    ROOT / "tools/reader_highlight_host_test.c", cjson / "cJSON.c", "-lm", "-o", exe], check=True)
    subprocess.run([exe], check=True)
