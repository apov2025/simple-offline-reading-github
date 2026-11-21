#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实查词后端 + SDK FatFs 的定位回归；只用临时数据，不接触卡或设备。
Exercise actual dictionary code through SDK FatFs with counted virtual disk reads.
No hardware, SD card, latency claims, or persistent build outputs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def fixture(path):
    """Enough clusters to exceed a 256-word map when each cluster is fragmented."""
    count = 32768
    data = bytearray()
    index = bytearray()
    start = 32 + count * 80
    for i in range(count):
        key = f"word{i:05d}"
        payload = json.dumps(dict(word=key, pronunciation="", senses=[dict(
            pos="", gloss="Deterministic definition. " * 12, synonyms=[])]),
            separators=(",", ":")).encode()
        index.extend(struct.pack("<64sQII", key.encode(), start + len(data), len(payload), 0))
        data.extend(payload)
    path.write_bytes(struct.pack("<8s6I", b"SORDICT1", 1, count, 32, start,
                                 start + len(data), 0) + index + data)


def samples(path):
    """Read known index positions directly to choose expectations; do not copy search."""
    with path.open("rb") as stream:
        magic, version, count, index, data, size, reserved = struct.unpack("<8s6I", stream.read(32))
        assert (magic, version, index, reserved) == (b"SORDICT1", 1, 32, 0)
        assert path.stat().st_size == size and data == index + count * 80 and count >= 10
        queries = []
        for row in (count * percent // 100 for percent in (17, 37, 61, 79, 93)):
            stream.seek(index + row * 80)
            key, offset, length, flags = struct.unpack("<64sQII", stream.read(80))
            assert not flags
            query = key.split(b"\0", 1)[0].decode("utf-8")
            stream.seek(offset)
            record = json.loads(stream.read(length))
            queries.extend((query, record["word"]))
        return queries


def copy_fatfs(sdk, destination):
    source = sdk / "components/fatfs/src"
    for name in ("ff.c", "ff.h", "diskio.h"):
        shutil.copyfile(source / name, destination / name)
    config = (source / "ffconf.h").read_text()
    # 保留真实定位/读数据代码，只移除测试不需要的 RTOS、写入和长文件名环境。
    # Keep real seek/read code; remove only RTOS, writes and long-name environment.
    config = config.replace('#include "sdkconfig.h"', """
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_VOLUME_COUNT 1
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_USE_DYN_BUFFERS 0
#define CONFIG_FATFS_USE_FASTSEEK 1
#define CONFIG_WL_SECTOR_SIZE 512
""")
    config = config.replace('#include "freertos/FreeRTOS.h"', "").replace('#include "freertos/semphr.h"', "")
    config = re.sub(r"(#define FF_FS_READONLY)\s+0", r"\1 1", config)
    config = re.sub(r"(#define FF_FS_REENTRANT)\s+1", r"\1 0", config)
    (destination / "ffconf.h").write_text(config)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", type=Path, default=Path(os.environ.get("IDF_PATH", str(Path.home() / "esp/esp-idf"))))
    parser.add_argument("--dictionary", type=Path, help="Optional existing dictionary, read-only; otherwise generate an isolated fixture")
    parser.add_argument("--source", type=Path, default=ROOT / "main/dictionary/reader_dictionary.c",
                        help="Backend source override for verifying a pre-fix failure")
    args = parser.parse_args()
    if not (args.idf_path / "components/fatfs/src/ff.c").is_file():
        parser.error("SDK FatFs not found; provide --idf-path or IDF_PATH")
    defaults = (ROOT / "sdkconfig.defaults").read_text()
    assert re.search(r"^CONFIG_FATFS_USE_FASTSEEK=y$", defaults, re.M), "Pico fast-seek default must be enabled"
    assert re.search(r"^CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=256$", defaults, re.M), "Probe expects the Pico 256-word bound"
    compiler = os.environ.get("CC", "cc")
    module = ROOT / "main/dictionary"
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    with tempfile.TemporaryDirectory(prefix="reader-dictionary-seek-") as temp:
        work = Path(temp)
        copy_fatfs(args.idf_path, work)
        dictionary = args.dictionary.resolve() if args.dictionary else work / "offline.dict"
        if not args.dictionary:
            fixture(dictionary)
        queries = samples(dictionary)
        common = [compiler, "-std=c11", "-D_DARWIN_C_SOURCE", "-D_POSIX_C_SOURCE=200809L",
                  "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                  "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-O1",
                  "-I", str(work), "-I", str(module), "-I", str(cjson)]
        symbols = ("fopen", "fclose", "fseek", "ftell", "fread", "ferror", "fileno", "fstat")
        adapter = work / "stdio-adapter.h"
        adapter.write_text("""#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
FILE *seek_test_fopen(const char *,const char *);
int seek_test_fclose(FILE *);
int seek_test_fseek(FILE *,long,int);
long seek_test_ftell(FILE *);
size_t seek_test_fread(void *,size_t,size_t,FILE *);
int seek_test_ferror(FILE *);
int seek_test_fileno(FILE *);
int seek_test_fstat(int,struct stat *);
""" + "".join(f"#define {name} seek_test_{name}\n" for name in symbols))
        backend = work / "backend.o"
        subprocess.run([*common, "-include", str(adapter),
                        "-c", str(args.source.resolve()), "-o", str(backend)], check=True)
        output = work / "seek-test"
        subprocess.run([*common, str(ROOT / "tools/reader_dictionary_seek_host_test.c"),
                        str(work / "ff.c"), str(cjson / "cJSON.c"), str(backend),
                        "-lm", "-o", str(output)], check=True)
        subprocess.run([str(output), str(dictionary), *queries], check=True)


if __name__ == "__main__":
    main()
