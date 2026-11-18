#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""校验板型选择与已确认接线配置。/ Validate board selection and explicit wiring profiles."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
cmake = shutil.which("cmake")
assert cmake, "cmake is required"
cases = [
    ({}, True),
    ({"READER_BOARD": "pico", "IDF_TARGET": "esp32s3"}, True),
    ({"READER_BOARD": "pico", "IDF_TARGET": "esp32s31"}, False),
    ({"READER_BOARD": "other"}, False),
    ({"READER_BOARD": "metalio"}, False),
    ({"READER_BOARD": "metalio", "METALIO_HW_REV": "1.4"}, False),
    ({"READER_BOARD": "metalio", "METALIO_PIN_PROFILE": "unknown"}, False),
    ({"READER_BOARD": "metalio", "METALIO_PIN_PROFILE": "plus-xstl4-mode19"}, True),
    ({"READER_BOARD": "metalio", "METALIO_PIN_PROFILE": "plus-xstl4-mode19", "IDF_TARGET": "esp32s3"}, False),
]
for values, accepted in cases:
    run = subprocess.run([cmake, *[f"-D{k}={v}" for k, v in values.items()],
                          "-P", str(root / "cmake/reader_target.cmake")], capture_output=True, text=True)
    assert (run.returncode == 0) == accepted, (values, run.stdout, run.stderr)
print("Reader target selection: 9 cases PASS")
