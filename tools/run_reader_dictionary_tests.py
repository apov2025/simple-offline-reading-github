"""构造词典并测试固件。/ Exercise firmware against generated dictionary fixtures."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "main/dictionary"
CJSON = ROOT / "managed_components/espressif__cjson/cJSON"
OK, MISS, MISSING, INVALID = range(4)
sense = dict(pos="动词", gloss="阅读文字", synonyms=["阅", "看"])
record = dict(word="读", pronunciation="dú", senses=[sense])


def pack(items):
    data = bytearray()
    index = bytearray()
    offset = 32 + 80 * len(items)
    for key, payload in items:
        index += struct.pack("<64sQII", key.encode(), offset + len(data), len(payload), 0)
        data += payload
    return struct.pack("<8s6I", b"SORDICT1", 1, len(items), 32, offset,
                       offset + len(data), 0) + index + data


def encode(obj):
    return json.dumps(obj, ensure_ascii=False).encode()


with tempfile.TemporaryDirectory() as temp:
    output = Path(temp) / "lookup"
    path = Path(temp) / "offline.dict"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f'-DREADER_DICT_DIRECTORY="{temp}"', "-I", MODULE, "-I", CJSON, MODULE / "reader_dictionary.c",
                    Path(__file__).with_name("reader_dictionary_host_test.c"),
                    CJSON / "cJSON.c", "-lm", "-o", output], check=True)

    def check(query, expected, selected=path):
        lines = subprocess.check_output([output, selected, query], text=True).splitlines()
        assert int(lines[0]) == expected, (query, expected, lines[:3])
        if expected != OK:
            assert lines[1:5] == ["", "", "0", "0"]
        return lines[1:]

    check("x", MISSING)
    base = pack([(key, encode(record)) for key in ["a", "don't read", "读"]])
    path.write_bytes(base)
    for key in ["A", " DON’T  Read\t", "读"]:
        assert check(key, OK) == ["读", "dú", "1", "0", "动词", "阅读文字", "阅, 看"]
    for key in ["", "do", "a" * 64, "閲"]:
        check(key, MISS)
    for bad in [base[:12], base[:-1], b"BADMAGIC" + base[8:]]:
        path.write_bytes(bad)
        check("a", INVALID)
    for at, value in [(8, 2), (12, 0xffffffff), (16, 0), (20, 1), (24, 0),
                      (28, 1), (96, 1), (100, 1), (104, 16385), (108, 1)]:
        damaged = bytearray(base)
        struct.pack_into("<I", damaged, at, value)
        path.write_bytes(damaged)
        check("a", INVALID)
    for key_field in [bytes(64), b"a" * 64, b"a\0junk".ljust(64, b"\0"),
                      b"\xc0\x80".ljust(64, b"\0")]:
        damaged = bytearray(base)
        damaged[32:96] = key_field
        path.write_bytes(damaged)
        check("a", INVALID)
    for payload in [b'{', b'{}', b' ' * 16385, encode(record).replace(b'd', b'\\u0000'), encode(record) + b'!', encode(dict(record, senses="x")),
                    encode(dict(record, senses=[dict(sense, synonyms=[3])])),
                    encode(record).replace("读".encode(), b'\xff'), b'[' * 1024 + b']' * 1024]:
        path.write_bytes(pack([("a", payload)]))
        check("a", INVALID)
    path.write_bytes(pack([("a", encode(dict(record, senses=[dict(sense, gloss="读" * 400)] + [sense] * 12)))]))
    result = check("a", OK)
    assert result[2:4] == ["12", "1"] and result[5] == "读" * 255
    # 旧教育部文件不再参与默认查词；只接受简明释义结构。/ Ignore the retired MOE file and require concise senses.
    moe = Path(temp) / "moe-mini.dict"
    original = {"word": "讀", "source": "教育部國語小字典", "full_text": "原文\n第二行"}
    moe.write_bytes(pack([("读", encode(original))]))
    path.write_bytes(base)
    assert check("读", OK, "-")[0] == "读"
    check("读", INVALID, moe)
    path.unlink()
    check("读", MISSING, "-")
    path.write_bytes(pack([("行", encode(dict(word="行", pronunciation="", source="测试来源", senses=[
        dict(sense, pronunciation="háng", gloss="行列。"),
        dict(sense, pronunciation="xíng", gloss="行走。")])))]))
    fields=check("行",OK)
    assert fields[-2:]==["háng", "xíng"]
    for bad in [3, {}, ["x"], None]:
        path.write_bytes(pack([("行", encode(dict(record, senses=[dict(sense,pronunciation=bad)])))]))
        check("行",INVALID)
    # 所有格只在精确条目缺失时还原；词形别名由已核对的词库提供。
    # Restore possessives only after an exact miss; validated dictionary aliases supply inflections.
    def english(word,gloss):
        return encode(dict(word=word,pronunciation="",source="测试词库",senses=[dict(pos="",gloss=gloss,synonyms=[])]))
    items={"jurisdiction":english("jurisdiction","管辖权"),"wife":english("wife","妻子"),
           "wives":english("wife","妻子"),"reader":english("reader","读者"),
           "readers":english("reader","读者"),"it's":english("it's","it is 或 it has 的缩写"),
           "it":english("it","它"),"let":english("let","让"),"news":english("news","新闻"),
           "james":english("James","詹姆斯"),"new york":english("New York","纽约"),
           "owner's":english("owner's","独立精确条目优先"),"owner":english("owner","主人")}
    path.write_bytes(pack(sorted(items.items())))
    for form,head in [("jurisdiction's","jurisdiction"),("JURISDICTION’S","jurisdiction"),
                      ("wives'","wife"),("WIVES’","wife"),("readers'","reader"),
                      ("reader’s","reader"),("James'","James"),("New York’s","New York")]:
        assert check(form,OK)[0]==head,(form,head)
    assert check("owner's",OK)[0]=="owner's"
    assert check("it's",OK)[0]=="it's"
    assert check("news",OK)[0]=="news"
    for form in ["let's","he's","'s","先's","reader''","nonsense's"]:
        check(form,MISS)
    path.write_bytes(pack([("jurisdiction",b'{')]))
    check("jurisdiction's",INVALID)
print("dictionary lookup and malformed fixtures passed (ASan/UBSan)")
