#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""用模拟器依赖跑阅读器状态测试，不操作真机或用户数据。
Run reader state tests with simulator dependencies, without touching hardware or user data.
"""
from pathlib import Path
import os
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SIM = ROOT / "sim"


def main():
    subprocess.run(["make", "-C", str(SIM), "-j4"], check=True, stdout=subprocess.DEVNULL)
    flags = shlex.split((SIM / "compile_flags.txt").read_text())
    with tempfile.TemporaryDirectory(prefix="reader-navigation-") as temp:
        work = Path(temp)
        # 保留模拟器环境函数，仅重命名窗口程序入口。/ Keep simulator environment functions, renaming only its program entry.
        subprocess.run(["cc", *flags, "-Dmain=sim_program_main", "-c", "src/sim_main.c", "-o", str(work / "sim_main.o")], cwd=SIM, check=True)
        subprocess.run(["cc", *flags, "-include", "idf/sim_fs.h", "-c", "tests/reader_navigation_test.c", "-o", str(work / "test.o")], cwd=SIM, check=True)
        objects = [p for p in (SIM / "build").rglob("*.o") if p.name not in ("reader_dictionary_ui.o", "reader_vocabulary_ui.o", "app_reader.o", "app_usb_transfer.o", "display.o", "sim_main.o")]
        libs = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
        if sys.platform == "darwin":
            libs += ["-framework", "Cocoa"]
        battery_test=work / "battery-test"
        subprocess.run(["cc","-I",str(ROOT / "main/ui"),str(ROOT / "tools/reader_battery_host_test.c"),"-o",str(battery_test)],check=True)
        subprocess.run([str(battery_test)],check=True)
        exe = work / "test"
        subprocess.run(["cc", *map(str, objects), str(work / "sim_main.o"), str(work / "test.o"), *libs, "-lz", "-lm", "-lpthread", "-o", str(exe)], check=True)
        (work / "data/sdcard/books").mkdir(parents=True)
        (work / "data/flash/books").mkdir(parents=True)
        for folder, name in (("flash", "Fallback.ttf"), ("sdcard", "Saved.ttf")):
            fonts = work / "data" / folder / "fonts"
            fonts.mkdir(parents=True)
            shutil.copy2(ROOT / "builtin/storage/fonts/PicoHei-Regular.ttf", fonts / name)
        (work / "data/flash/fonts" / ("LongFont" + "x" * 180 + ".ttf")).write_bytes(b"invalid")
        dictionary = work / "data/sdcard/dict/offline.dict"
        dictionary.parent.mkdir()
        words = {
            "山川之间": ("shān chuān zhī jiān", [{"pos": "短语", "gloss": "山与河之间。"}]),
            "心": ("xīn", [{"gloss": "心脏；心思。"}]),
            "一心一意": ("yī xīn yī yì", [{"pos": "成语", "pronunciation": "yī xīn yī yì", "gloss": "心思专一，没有别的念头。"}]),
            "co-operate": ("", [{"gloss": "合作。"}]),
            "co": ("", [{"gloss": "共同。"}]),
            "the": ("", [{"gloss": "定冠词。"}]),
            "operate": ("", [{"gloss": "操作；运作。"}]),
            "quiet": ("英 /ˈkwaɪət/", [
                {"pos": "形容词", "gloss": "安静的，平静的。", "synonyms": ["calm", "still"]},
                {"pos": "名词", "gloss": "平静。" * 65, "synonyms": []},
            ]),
            "走": ("zǒu", [{"pos": "", "gloss": gloss, "synonyms": []} for gloss in
                           ("步行；人或动物移动脚前进。", "离开；离去。", "移动；挪动。", "通过；经过。", "往来；交往。")]),
            "行": ("xíng", [{"gloss": "走；步行。", "pronunciation": "xíng"}, {"gloss": "行列；行业。", "pronunciation": "háng"}]),
            "旧": ("jiù", [{"gloss": "旧格式测试"}]),
            "山": ("shān", [{"pos": "名词", "gloss": "地面上高耸的部分。", "synonyms": []}]),
        }
        index = bytearray()
        data = bytearray()
        base = 32 + 80 * len(words)
        for word, (pronunciation, senses) in sorted(words.items()):
            for sense in senses:
                sense.setdefault("pos", "")
                sense.setdefault("synonyms", [])
            entry = dict(word=word, pronunciation=pronunciation, senses=senses)
            entry["source"] = "测试词典 · CC BY-SA 4.0"
            if word == "旧":
                entry["full_text"] = "部首：旧 注音：ㄐㄧㄡˋ 旧资料应拒绝"
            record = json.dumps(entry, ensure_ascii=False).encode()
            index.extend(struct.pack("<64sQII", word.encode(), base + len(data), len(record), 0))
            data.extend(record)
        dictionary.write_bytes(struct.pack("<8s6I", b"SORDICT1", 1, len(words), 32, base, base + len(data), 0) + index + data)
        if os.environ.get("READER_SIMPLIFIED_SAMPLE"):
            shutil.copyfile(os.environ["READER_SIMPLIFIED_SAMPLE"], dictionary.parent / "candidate.dict")
        book=work/'data/sdcard/books/toc.epub'
        with zipfile.ZipFile(book,'w') as z:
            z.writestr('META-INF/container.xml','<container><rootfiles><rootfile full-path="book.opf"/></rootfiles></container>')
            z.writestr('book.opf','<package><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/><item id="broken" href="broken.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine><itemref idref="body"/><itemref idref="broken"/></spine></package>')
            z.writestr('nav.xhtml','<html><body><nav epub:type="toc"><ol><li><a href="body.xhtml">第一章</a><ol><li><a href="body.xhtml#part">第二节</a></li><li><a href="body.xhtml#missing">失效小节</a></li></ol></li>'+''.join(f'<li><a href="body.xhtml#extra{i}">更多章节 {i}</a></li>' for i in range(10))+'<li><a href="broken.xhtml">损坏章节</a></li></ol></nav></body></html>')
            z.writestr('body.xhtml','<html><body><h1>第一章</h1>'+('<p>山川与河流，阅读中的故事。The quiet reader turns another page.</p>'*120)+'<h2 id="part">第二节</h2>'+('<p>这是第二节的正文。</p>'*20)+''.join(f'<h2 id="extra{i}">更多章节 {i}</h2>'+('<p>这是当前章节的测试正文。</p>'*40) for i in range(10))+'</body></html>')
            z.writestr('broken.xhtml','<html><body><p>Broken fixture.</p></body></html>')
        manual=work / 'data/flash/manual/pico-guide.epub'
        manual.parent.mkdir()
        shutil.copyfile(os.environ.get('READER_MANUAL_SAMPLE') or book, manual)
        with zipfile.ZipFile(book) as z:
            start=z.getinfo('broken.xhtml').header_offset
        raw=bytearray(book.read_bytes())
        name_len,extra_len=struct.unpack_from('<HH',raw,start+26)
        raw[start+30+name_len+extra_len]^=1
        book.write_bytes(raw)
        subprocess.run([str(exe)], cwd=SIM, env={**os.environ, "SIM_DATA": str(work / "data")}, check=True)


if __name__ == "__main__":
    main()
