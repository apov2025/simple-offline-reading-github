#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""临时数据与故障注入验证单词本，不访问用户卡。/ Exercise notebook with temporary data and injected failures, never a user card."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reader-vocabulary-") as temp:
    work = Path(temp)
    exe = work / "test"
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f'-DREADER_VOCABULARY_DIRECTORY="{work / "notes"}"',
                    "-I", ROOT / "sim/idf", "-I", ROOT / "main", "-I", ROOT / "main/dictionary",
                    "-I", ROOT / "components/read_pico/include", "-I", cjson,
                    ROOT / "tools/reader_vocabulary_host_test.c", cjson / "cJSON.c", "-lm", "-o", exe], check=True)
    subprocess.run([exe], check=True)
