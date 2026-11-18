#!/usr/bin/env python3
"""生成机身自带的阅读字体：把一个开源字体裁成"单一字重 + 常用字"，放进 builtin/storage/fonts。

Build the reading font that ships inside the device: instance an open font at one
weight, keep the commonly used characters, and write it to builtin/storage/fonts.

收哪些字 / What is kept:
  - ASCII、西欧字母、常用标点符号 / ASCII, Western European letters, common punctuation
  - GBK 的全部汉字（约 2.1 万个，人名地名里的生僻字基本都在），以及 GB2312 里的符号、假名、希腊和俄文字母
    every Han character of GBK (about 21,000, covering nearly all rare characters in names) plus the
    symbols, kana, Greek and Cyrillic of GB2312
  - 固件界面字体里已有的字 / every character of the firmware's UI font
  - 内置书里出现过的字 / every character used by the built-in books

源字体是 OFL 许可且有保留字体名，裁剪版属于修改版，所以必须改名，并随字体带上 OFL.txt。
The source is under the OFL with Reserved Font Names; a subset is a Modified Version, so it is
renamed and ships with OFL.txt.

用法 / Usage:  python3 tools/gen_reading_font.py   (需要 fontTools / needs fontTools)
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import zipfile
from html import unescape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "sdcard/fonts/ChillDuanSansVF.ttf"
LICENSE = ROOT / "sdcard/fonts/OFL.txt"
UI_FONT = ROOT / "main/assets/builtin.ttf"
BOOKS = ROOT / "builtin/storage/books"
OUT = ROOT / "builtin/storage/fonts/PicoHei-Regular.ttf"
FAMILY = "Pico Hei"
WEIGHT = 400


def gb2312() -> set[str]:
    chars: set[str] = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


def gbk_han() -> set[str]:
    chars: set[str] = set()
    for cp in range(0x4E00, 0xA000):
        try:
            chr(cp).encode("gbk")
            chars.add(chr(cp))
        except UnicodeEncodeError:
            pass
    return chars


def book_chars(folder: Path) -> set[str]:
    chars: set[str] = set()
    for path in sorted(folder.glob("*.epub")):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if not name.lower().endswith((".xhtml", ".html", ".htm", ".opf", ".ncx")):
                    continue
                text = archive.read(name).decode("utf-8", "replace")
                chars.update(unescape(re.sub(r"<[^>]*>", "", text)))
    return {c for c in chars if c >= " "}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()

    try:
        from fontTools.subset import Options, Subsetter
        from fontTools.ttLib import TTFont
        from fontTools.varLib.instancer import instantiateVariableFont
    except ImportError:
        print("需要 fontTools：pip install fonttools / fontTools is required", file=sys.stderr)
        return 1

    wanted = {chr(i) for i in range(0x20, 0x7F)}
    wanted |= {chr(i) for i in range(0xA0, 0x180)}       # 西欧字母 / Latin-1 and Latin Extended-A
    wanted |= {chr(i) for i in range(0x2010, 0x2028)}    # 连接号、引号、省略号 / dashes, quotes, ellipsis
    wanted |= {chr(i) for i in range(0x2030, 0x203C)}
    wanted |= {chr(i) for i in range(0x3000, 0x3040)}    # 中文标点 / CJK punctuation
    wanted |= {chr(i) for i in range(0xFF00, 0xFFF0)}    # 全角字符 / fullwidth forms
    wanted |= set("€£¥©®™°±×÷→←↑↓•◦※〇")
    wanted |= gb2312()
    wanted |= gbk_han()
    if UI_FONT.is_file():
        wanted |= {chr(cp) for cp in TTFont(UI_FONT).getBestCmap()}
    in_books = book_chars(BOOKS) if BOOKS.is_dir() else set()
    wanted |= in_books

    font = TTFont(args.source)
    if "fvar" in font:
        font = instantiateVariableFont(font, {"wght": WEIGHT}, inplace=False)
    have = set(font.getBestCmap())
    missing = sorted(c for c in in_books if ord(c) not in have and not c.isspace())

    options = Options()
    options.layout_features = []      # 固件不做字形替换 / the firmware applies no OpenType features
    options.notdef_outline = True
    options.ignore_missing_glyphs = True
    options.ignore_missing_unicodes = True
    options.hinting = False
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    subsetter = Subsetter(options=options)
    subsetter.populate(unicodes=[ord(c) for c in wanted])
    subsetter.subset(font)

    # 改名：保留版权和许可，不再使用源字体的保留名。/ Rename: keep copyright and license, drop the reserved names.
    name = font["name"]
    for record in list(name.names):
        if record.nameID not in (0, 13, 14):
            name.removeNames(nameID=record.nameID)
    for name_id, value in (
        (1, FAMILY), (2, "Regular"), (3, f"{FAMILY} Regular; subset for Read Pico"), (4, f"{FAMILY} Regular"),
        (5, "Version 1.0"), (6, FAMILY.replace(" ", "") + "-Regular"),
    ):
        name.setName(value, name_id, 3, 1, 0x409)
        name.setName(value, name_id, 1, 0, 0)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.out)
    if LICENSE.is_file():
        shutil.copyfile(LICENSE, args.out.parent / "OFL.txt")
    size = args.out.stat().st_size
    print(f"{args.out.relative_to(ROOT)}: {len(font.getGlyphOrder())} glyphs, {size / 1e6:.2f} MB")
    if missing:
        print(f"内置书里有 {len(missing)} 个字源字体没有 / characters in the built-in books missing from the source: {''.join(missing)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
