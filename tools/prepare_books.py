#!/usr/bin/env python3
"""把下载来的 EPUB 整理成阅读器好读的样子：书名、作者、年份写清楚，文件重新打包。

Prepare downloaded EPUBs for the reader: clear title, author and year, and a clean repack.

阅读器不整理书，这件事在电脑上做。分两步，中间那份清单可以交给 AI 或人过目、改正：
The reader does not tidy books; that happens on the computer, in two steps with a plan in between
that an AI or a person can review and correct:

    python3 tools/prepare_books.py scan  <放原书的文件夹> -o plan.json     看每本书，提出建议 / inspect and propose
    python3 tools/prepare_books.py apply plan.json -o <输出文件夹>         按清单写出整理好的书 / write prepared books

    python3 tools/prepare_books.py <放原书的文件夹> -o <输出文件夹>        两步连着做 / both in one go

原书不会被改动。整理好的书：
The originals are never modified. A prepared book has:
  - 书里的书名、作者、年份改成清单里定下的 / the plan's title, author and year written into the book
  - 文件名统一成"书名 - 作者 (年份).epub" / a file name of the form "<title> - <author> (<year>).epub"
  - 重新打包成最普通的 ZIP（图片不再压缩，设备读起来更快）/ a plain ZIP repack (images stored, faster on the device)
  - 封面提前缩成屏幕大小的灰度图放在书里，设备锁屏时直接显示 / the cover pre-rendered as a screen-sized
    gray image inside the book, which the device shows on its lock screen
    （需要 Pillow，或 macOS 自带的 sips；都没有就不做封面 / needs Pillow or macOS's sips; skipped without either）

书名和作者怎么定：把书里自带的信息、文件名、书的头几页文字放在一起看，哪个像人名、哪个像书名、
互相对不对得上，而不是按固定格式硬拆。拿不准的标成"请确认"并写明原因，交给 AI 或人定。
How title and author are decided: the book's own metadata, its file name and the text of its first
pages are weighed together (what looks like a person's name, what looks like a title, whether they
agree) rather than split by a fixed format. Doubtful cases are marked "check" with the reason.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from urllib.parse import unquote
from html import unescape
from pathlib import Path
from xml.sax.saxutils import escape

# 设备的限制，超过了要提醒。/ Device limits worth a warning.
CHAPTER_MAX = 2 * 1024 * 1024
NAME_BYTES_MAX = 190

# 设备锁屏图的格式：整屏 4 位灰度，每字节两个像素（先低四位），0 黑 15 白。
# The device's lock image format: full-screen 4-bit gray, two pixels per byte (low nibble first), 0 black to 15 white.
SCREEN_W, SCREEN_H = 684, 1216
COVER_PATH = "pico/cover.4bpp"
IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp")

SITE_TAGS = re.compile(r"[\(\[（【][^\)\]）】]*(z-lib|z-library|zlibrary|libgen|anna'?s[- ]archive|epub|mobi|azw3|pdf)[^\)\]）】]*[\)\]）】]", re.I)
HASH_TAIL = re.compile(r"[_\- ][0-9a-f]{8,}$", re.I)
OPENERS, CLOSERS = "([（【", ")]）】"
NATION = re.compile(r"^\s*[\(\[（【〔]\s*[^\)\]）】〕]{1,6}\s*[\)\]）】〕]\s*")
ROLE_CUT = re.compile(r"\s*(?:[;；]|\s著|\s编|\s译|\s绘|\s图|\s等|著；|、|\sand\s|\s&\s)")
PUBLISHER_WORDS = re.compile(r"(press|books|publishing|publishers|university|portfolio|出版社|出版集团|书店)", re.I)
TITLE_OPENERS = {"the", "a", "an", "how", "why", "what", "when", "on", "in", "of", "to", "my", "our"}
JUNK_VALUES = {"", "unknown", "untitled", "未知", "佚名", "anonymous", "calibre"}


# ---- 文字清理 / text cleanup ----

def trim(text: str) -> str:
    return text.strip(" _　\t").rstrip(":：,，-–—").strip(" _　")


def strip_trailing_groups(text: str) -> tuple[str, list[str]]:
    """去掉挂在末尾的括号，返回剩下的文字和被去掉的各段。Drop trailing bracketed groups; return the rest and what was dropped."""
    dropped: list[str] = []
    while True:
        text = trim(text)
        if not text or text[-1] not in CLOSERS:
            return text, dropped
        depth, start = 0, None
        for i in range(len(text) - 1, -1, -1):
            if text[i] in CLOSERS:
                depth += 1
            elif text[i] in OPENERS:
                depth -= 1
                if depth == 0:
                    start = i
                    break
        if start is None or start == 0:
            return text, dropped
        inner = text[start + 1:-1].strip("()（）[]【】 ")
        dropped.append(inner)
        text = text[:start]


def clean_title(text: str) -> tuple[str, list[str]]:
    text, dropped = strip_trailing_groups(unescape(text or ""))
    if text.startswith("《") and text.endswith("》") and len(text) > 2:
        text = text[1:-1]
    return trim(text), dropped


def clean_person(text: str) -> str:
    text = trim(unescape(text or ""))
    text = NATION.sub("", text)
    text = ROLE_CUT.split(text, maxsplit=1)[0]
    text, _ = strip_trailing_groups(text)
    if text.isascii() and text.count(",") == 1:      # "Last, First" -> "First Last"
        last, first = (part.strip() for part in text.split(","))
        if first and last:
            text = f"{first} {last}"
    return trim(text)


def split_people(text: str) -> list[str]:
    """一个作者栏里可能挤着几个人。A single creator field may hold several people."""
    text = unescape(text or "")
    parts = [p for p in text.split(",")]
    # "Faber, Adele, Mazlish, Elaine"：四段、全是单个词，是两组"姓, 名"。/ four single words: two "Last, First" pairs.
    if text.isascii() and len(parts) >= 4 and len(parts) % 2 == 0 and all(len(p.split()) <= 2 for p in parts):
        return [clean_person(f"{parts[i]},{parts[i + 1]}") for i in range(0, len(parts), 2)]
    people = []
    for piece in re.split(r"\s+and\s+|\s*&\s*|[;；、]|\s{2,}", text):
        # "某某 编""某某 译""某某 绘"不是作者，整个人去掉；"某某 著"留下名字。
        # "X 编", "X 译", "X 绘" are not authors and are dropped whole; "X 著" keeps the name.
        if re.search(r"\s(编|译|绘|图|校|注|编著|主编|翻译)\s*$", piece.strip()):
            continue
        people.append(clean_person(piece))
    return [p for p in people if p and not re.fullmatch(r"(著|编|译|绘|图|等)", p)]


def looks_like_person(text: str) -> bool:
    """像不像人名。英文：二到四个大写开头的词，不带数字冒号，不以书名常用词开头。
    中文：二到四个汉字，或带间隔号的译名。
    Whether text looks like a person's name. Latin: two to four capitalized words without digits or
    colons, not opening with a word titles favor. Chinese: two to four Han characters, or a dotted
    transliteration."""
    name = clean_person(text)
    if not 2 <= len(name) <= 40:
        return False
    if name.isascii():
        words = name.split()
        if not 2 <= len(words) <= 4 or words[0].lower() in TITLE_OPENERS:
            return False
        return all(w[0].isupper() and not re.search(r"[\d:_]", w) for w in words)
    if re.search(r"[\d：:，,。！？!?《》]", name):
        return False
    dotted = any(mark in name for mark in "·•.")
    han = re.sub(r"[·•.\sA-Za-z]", "", name)
    return (2 <= len(name) <= 4 and " " not in name) or (dotted and len(han) <= 14)


def could_be_person(text: str) -> bool:
    """宽松的判断，用来检查书里填的作者：只挑出明显不是人名的。
    A lenient check for a book's stated author: flags only what is clearly not a name."""
    name = clean_person(text)
    if not name or re.search(r"[\d：:，,。！？!?《》]", name):
        return False
    if name.isascii():
        return 1 <= len(name.split()) <= 5 and not name.islower()
    return len(re.sub(r"[·•.\sA-Za-z]", "", name)) <= 12


def title_like(text: str) -> bool:
    """明显是书名而不是人名：带冒号、书名号，或者是一长句。Clearly a title rather than a name: colons, title marks, or a long phrase."""
    return bool(re.search(r"[：:《》——]", text)) or len(text.split()) >= 5 or len(re.sub(r"[\sA-Za-z·•.]", "", text)) > 12


def year_of(text: str) -> str:
    match = re.match(r"\s*(\d{4})", text or "")
    return match.group(1) if match and 1000 <= int(match.group(1)) <= 2100 else ""


# ---- 读一本书 / reading one book ----

def read_book(path: Path) -> dict:
    found: dict = {"opf_titles": [], "opf_creators": [], "opf_date": "", "publisher": "", "first_pages": "", "problems": []}
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        container = archive.read("META-INF/container.xml").decode("utf-8", "replace")
        opf_path = re.search(r'full-path="([^"]+)"', container).group(1)
        opf = archive.read(opf_path).decode("utf-8", "replace")
        found["opf_path"] = opf_path

        def elements(tag: str) -> list[tuple[str, str]]:
            return [(attrs, unescape(re.sub(r"<[^>]+>", "", body)).strip())
                    for attrs, body in re.findall(rf"<dc:{tag}\b([^>]*)>(.*?)</dc:{tag}>", opf, re.S)]

        found["opf_titles"] = [text for _, text in elements("title") if text]
        # 标了角色的只要作者（aut），译者、绘者不算。/ With roles given, keep authors (aut) only.
        creators = elements("creator")
        authors = [text for attrs, text in creators if not re.search(r'role="(?!aut)', attrs)]
        found["opf_creators"] = [text for text in (authors or [t for _, t in creators]) if text]
        dates = [text for _, text in elements("date")]
        found["opf_date"] = next((y for y in map(year_of, dates) if y), "")
        found["publisher"] = next((text for _, text in elements("publisher")), "")

        # 书的头几页：书名页、版权页上的字，用来和书名作者对一对。
        # The first pages (title page, copyright page) to cross-check title and author.
        base = opf_path.rsplit("/", 1)[0] + "/" if "/" in opf_path else ""
        hrefs = dict(re.findall(r'<item\b[^>]*?\bid="([^"]+)"[^>]*?\bhref="([^"]+)"', opf))
        hrefs.update({i: h for h, i in re.findall(r'<item\b[^>]*?\bhref="([^"]+)"[^>]*?\bid="([^"]+)"', opf)})
        spine = re.findall(r'<itemref\b[^>]*?\bidref="([^"]+)"', opf)
        pages = []
        for idref in spine:
            name = base + unescape(hrefs.get(idref, ""))
            if name not in names:
                continue
            info = archive.getinfo(name)
            if info.file_size > CHAPTER_MAX:
                found["problems"].append(f"有一章解压后 {info.file_size // 1024} KB，超过设备的 2 MB 上限，这一章读不了：{name}")
            if len(pages) < 4:
                html = archive.read(name).decode("utf-8", "replace")
                pages.append(unescape(re.sub(r"<[^>]+>", " ", re.sub(r"<(script|style)\b.*?</\1>", " ", html, flags=re.S))))
        found["first_pages"] = re.sub(r"\s+", " ", " ".join(pages))[:6000]
        if not spine:
            found["problems"].append("书里没有正文目录（spine），设备打不开")
        if "META-INF/encryption.xml" in names:
            encryption = archive.read("META-INF/encryption.xml").decode("utf-8", "replace")
            targets = re.findall(r'CipherReference\s+URI="([^"]+)"', encryption)
            if any(not t.lower().endswith((".ttf", ".otf", ".woff", ".woff2")) for t in targets):
                found["problems"].append("书的内容有加密（DRM），设备读不了")
    return found


def name_parts(stem: str) -> dict:
    """把文件名拆成候选：年份、可能的两段、末尾括号。Split a file name into candidates: year, two parts, trailing bracket."""
    text = SITE_TAGS.sub(" ", stem)
    text = HASH_TAIL.sub("", text)
    text = re.sub(r"(?<=\S)_ ", ": ", text).replace("_", " ")
    text = trim(re.sub(r"\s+", " ", text))
    year = ""
    match = re.search(r"[\(（](\d{4})[\)）]\s*$", text)
    if match and year_of(match.group(1)):
        year, text = match.group(1), trim(text[:match.start()])
    parts: list[str] = []
    for sep in (" - ", " by ", "：", ": ", "-"):
        if sep in text.strip(sep):
            left, right = (text.split(sep, 1) if sep != " - " else text.split(sep, 1))
            parts = [trim(left), trim(right)]
            break
    body, groups = strip_trailing_groups(text)
    return {"text": text, "year": year, "parts": parts, "body": body, "groups": groups}


def appears(needle: str, haystack: str) -> bool:
    squeeze = lambda t: re.sub(r"[\s·•.,，、:：\-—_'’\"“”]", "", t).lower()
    needle = squeeze(needle)
    return len(needle) >= 2 and needle in squeeze(haystack)


def propose(path: Path) -> dict:
    entry: dict = {"file": path.name, "title": "", "author": "", "year": "", "confidence": "high", "why": []}
    try:
        found = read_book(path)
    except Exception as error:  # 打不开的书照样列出来 / unreadable books are still listed
        entry.update(confidence="check", why=[f"这个文件打不开：{error}"], problems=["不是可读的 EPUB"])
        entry["title"] = path.stem
        return entry
    name = name_parts(path.stem)
    pages = found.pop("first_pages")
    why: list[str] = entry["why"]

    opf_title, dropped = clean_title(found["opf_titles"][0] if found["opf_titles"] else "")
    people: list[str] = []
    for creator in found["opf_creators"]:
        people += [p for p in split_people(creator) if p.lower() not in JUNK_VALUES]
    people = list(dict.fromkeys(people))
    if opf_title.lower() in JUNK_VALUES:
        opf_title = ""

    # 书里把书名和作者填反了：书名栏像人名、作者栏不像。/ Metadata filled in backwards: the title looks like a name and the creator does not.
    # 只在很有把握时才调换：书名栏像人名，而作者栏明显是书名（带冒号、书名号或是一长句）。
    # 两三个字的中文书名本来就像人名（《群魔》《活着》），单凭这一点不能调换。
    # Swap only on strong evidence: the title looks like a name and the creator is clearly a title. A
    # two or three character Chinese title looks like a name anyway and is not enough on its own.
    if opf_title and people and looks_like_person(opf_title) and title_like(people[0]):
        why.append(f"书里的书名「{opf_title}」像人名、作者「{people[0]}」明显是书名，按填反了处理")
        opf_title, people = clean_title(people[0])[0], [clean_person(opf_title)]
        entry["confidence"] = "check"

    title, authors = opf_title, people[:]
    if not title or not authors:
        # 书里缺的，从文件名里找：哪段像人名哪段就是作者。/ What the book lacks comes from the file name: the part that looks like a name is the author.
        candidates = [p for p in name["parts"] if p] or [name["body"]]
        persons = [c for c in candidates if looks_like_person(c)] + [g for g in name["groups"] if looks_like_person(g)]
        others = [c for c in candidates if not looks_like_person(c)]
        if not authors and persons:
            authors = [clean_person(persons[0])]
            why.append(f"书里没写作者，从文件名里取「{authors[0]}」（它像人名）")
        if not title:
            guess = others[0] if others else (candidates[-1] if len(candidates) > 1 and not authors else candidates[0])
            title = clean_title(guess)[0]
            why.append(f"书里没写书名，从文件名里取「{title}」")
        entry["confidence"] = "check"

    # 书名末尾的"-出版社"是下载站加的。/ A trailing "-Publisher" comes from the download site.
    for sep in (" - ", "-"):
        head, _, tail = title.rpartition(sep)
        if head and tail and (appears(tail, found["publisher"]) or PUBLISHER_WORDS.search(tail)):
            title = trim(head)

    # 交叉核对：书名、作者在书的头几页或文件名里出现过才算稳。/ Cross-check: title and author should appear in the first pages or the file name.
    if title and not (appears(title, pages) or appears(title.split("：")[0].split(":")[0], name["text"])):
        why.append("书名在书的头几页和文件名里都没出现")
        entry["confidence"] = "check"
    if authors:
        first = authors[0]
        if not (appears(first, pages) or appears(first, name["text"])):
            why.append(f"作者「{first}」在书的头几页和文件名里都没出现")
            entry["confidence"] = "check"
        if not could_be_person(first):
            why.append(f"作者「{first}」不像人名（可能是笔名、机构，也可能填错了）")
            entry["confidence"] = "check"
    else:
        why.append("没找到作者")
        entry["confidence"] = "check"
    if dropped:
        why.append("去掉了书名后面的括号：" + "；".join(dropped))

    # 年份：文件名里写了就用它（多半是这一版的出版年），否则用书里的日期。
    # Year: the file name's when present (usually this edition's), else the book's own date.
    year = name["year"] or found["opf_date"]
    if not year:
        why.append("没找到年份")
    joiner = ", " if all(a.isascii() for a in authors[:2]) else "、"
    entry.update(title=title, author=joiner.join(authors[:2]), year=year)
    entry["found"] = {"书里的书名": found["opf_titles"], "书里的作者": found["opf_creators"], "书里的年份": found["opf_date"],
                      "出版社": found["publisher"]}
    if found["problems"]:
        entry["problems"] = found["problems"]
    return entry


# ---- 封面 / cover ----

def find_cover(archive: zipfile.ZipFile, opf_path: str, opf: str) -> str | None:
    """找出封面图在书里的路径。Locate the cover image inside the book."""
    names = set(archive.namelist())
    base = opf_path.rsplit("/", 1)[0] + "/" if "/" in opf_path else ""
    items = []
    for tag in re.findall(r"<item\b[^>]*>", opf):
        attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', tag))
        if "href" in attrs:
            items.append(attrs)

    def resolve(href: str) -> str | None:
        name = base + unquote(unescape(href)).split("#")[0]
        parts: list[str] = []
        for part in name.split("/"):
            if part == "..":
                parts and parts.pop()
            elif part not in ("", "."):
                parts.append(part)
        name = "/".join(parts)
        return name if name in names and name.lower().endswith(IMAGE_SUFFIXES) else None

    by_id = {a.get("id", ""): a for a in items}
    cover_id = None
    for tag in re.findall(r"<meta\b[^>]*>", opf):
        attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', tag))
        if attrs.get("name") == "cover":
            cover_id = attrs.get("content")
    candidates = []
    if cover_id in by_id:
        candidates.append(by_id[cover_id]["href"])
    candidates += [a["href"] for a in items if "cover-image" in a.get("properties", "")]
    candidates += [a["href"] for a in items if "cover" in (a.get("id", "") + a["href"]).lower()]
    for href in candidates:
        name = resolve(href)
        if name:
            return name
    # 没标明封面：取正文第一个文件里的第一张图。/ No declared cover: the first image of the first spine document.
    spine = re.findall(r'<itemref\b[^>]*?\bidref="([^"]+)"', opf)
    if spine and spine[0] in by_id:
        first = resolve_doc = base + unquote(by_id[spine[0]]["href"])
        if first in names:
            html = archive.read(first).decode("utf-8", "replace")
            match = re.search(r'<(?:img|image)\b[^>]*?(?:src|xlink:href)="([^"]+)"', html)
            if match:
                doc_base = first.rsplit("/", 1)[0] + "/" if "/" in first else ""
                name = "/".join(p for p in (doc_base + unquote(match.group(1))).split("/"))
                parts: list[str] = []
                for part in name.split("/"):
                    if part == "..":
                        parts and parts.pop()
                    elif part not in ("", "."):
                        parts.append(part)
                name = "/".join(parts)
                if name in names and name.lower().endswith(IMAGE_SUFFIXES):
                    return name
    return None


def fit_size(width: int, height: int) -> tuple[int, int]:
    scale = min(SCREEN_W / width, SCREEN_H / height)
    return max(1, round(width * scale)), max(1, round(height * scale))


def decode_gray(data: bytes, suffix: str) -> tuple[int, int, bytes] | None:
    """把图片解成缩到屏幕以内的 8 位灰度。Decode an image to 8-bit gray scaled to fit the screen."""
    try:
        from PIL import Image
        image = Image.open(io.BytesIO(data)).convert("L")
        size = fit_size(*image.size)
        image = image.resize(size, Image.LANCZOS)
        return size[0], size[1], image.tobytes()
    except ImportError:
        pass
    except Exception:
        return None
    if not shutil.which("sips"):
        return None
    with tempfile.TemporaryDirectory() as folder:
        source, target = Path(folder) / ("in" + suffix), Path(folder) / "out.bmp"
        source.write_bytes(data)
        info = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(source)], capture_output=True, text=True).stdout
        dims = re.findall(r"pixel(?:Width|Height): (\d+)", info)
        if len(dims) != 2:
            return None
        width, height = fit_size(int(dims[0]), int(dims[1]))
        done = subprocess.run(["sips", "-s", "format", "bmp", "-z", str(height), str(width), str(source), "--out", str(target)],
                              capture_output=True)
        if done.returncode != 0 or not target.is_file():
            return None
        return read_bmp(target.read_bytes())


def read_bmp(data: bytes) -> tuple[int, int, bytes] | None:
    if data[:2] != b"BM":
        return None
    le = lambda at, n: int.from_bytes(data[at:at + n], "little", signed=(n == 4 and at == 22))
    offset, header, width, height, bits = le(10, 4), le(14, 4), le(18, 4), le(22, 4), le(28, 2)
    if bits not in (8, 24, 32) or le(30, 4) not in (0, 3):
        return None
    flipped, height = height > 0, abs(height)
    stride = (width * bits // 8 + 3) & ~3
    palette = data[14 + header:14 + header + 1024] if bits == 8 else b""
    gray = bytearray(width * height)
    for y in range(height):
        row = data[offset + (height - 1 - y if flipped else y) * stride:][:stride]
        for x in range(width):
            if bits == 8:
                b, g, r = palette[row[x] * 4:row[x] * 4 + 3]
            else:
                b, g, r = row[x * (bits // 8):x * (bits // 8) + 3]
            gray[y * width + x] = (r * 299 + g * 587 + b * 114) // 1000
    return width, height, bytes(gray)


def make_cover(data: bytes, suffix: str) -> bytes | None:
    """封面图 → 设备的锁屏图：居中放在白底上，拉开对比，抖动成 16 级灰。
    Cover image to the device's lock image: centered on white, contrast stretched, dithered to 16 grays."""
    decoded = decode_gray(data, suffix)
    if decoded is None:
        return None
    width, height, gray = decoded
    # 拉开对比：墨水屏上灰蒙蒙的封面很难看。取 1% 和 99% 处的亮度作黑白两端。
    # Stretch contrast (a washed-out cover looks poor on e-ink) between the 1st and 99th percentiles.
    histogram = [0] * 256
    for value in gray:
        histogram[value] += 1
    def percentile(share: float) -> int:
        target, seen = share * len(gray), 0
        for level, count in enumerate(histogram):
            seen += count
            if seen >= target:
                return level
        return 255
    low, high = percentile(0.01), percentile(0.99)
    if high - low < 32:
        low, high = 0, 255
    left, top = (SCREEN_W - width) // 2, (SCREEN_H - height) // 2
    levels = bytearray([15]) * (SCREEN_W * SCREEN_H)
    # Floyd–Steinberg 误差扩散。/ Floyd–Steinberg error diffusion.
    ahead = [0.0] * (width + 2)
    for y in range(height):
        current, ahead = ahead, [0.0] * (width + 2)
        carry = 0.0
        row = gray[y * width:(y + 1) * width]
        base = (top + y) * SCREEN_W + left
        for x in range(width):
            value = (row[x] - low) * 255.0 / (high - low) + carry + current[x + 1]
            level = 0 if value <= 0 else 15 if value >= 255 else int(value / 17.0 + 0.5)
            levels[base + x] = level
            error = value - level * 17.0
            carry = error * 0.4375
            ahead[x] += error * 0.1875
            ahead[x + 1] += error * 0.3125
            ahead[x + 2] += error * 0.0625
    packed = bytearray(SCREEN_W * SCREEN_H // 2)
    for i in range(len(packed)):
        packed[i] = levels[2 * i] | (levels[2 * i + 1] << 4)
    return bytes(packed)


# ---- 写出整理好的书 / writing a prepared book ----

def output_name(entry: dict) -> str:
    safe = lambda t: re.sub(r'[\\/*?"<>|]', " ", t.replace(":", "：")).strip()
    title, author, year = safe(entry["title"]), safe(entry.get("author", "")), entry.get("year", "")
    tail = (f" - {author}" if author else "") + (f" ({year})" if year else "") + ".epub"
    while len((title + tail).encode()) > NAME_BYTES_MAX and len(title) > 4:
        title = title[:-1]
    return title + tail


def rewrite_opf(opf: str, entry: dict) -> str:
    removed = re.findall(r'<dc:(?:title|creator|date)\b[^>]*?\bid="([^"]+)"', opf)
    opf = re.sub(r"\s*<dc:(title|creator|date)\b[^>]*>.*?</dc:\1>", "", opf, flags=re.S)
    opf = re.sub(r"\s*<dc:(title|creator|date)\b[^>]*/>", "", opf)
    for ident in removed:   # 指着被删元素的补充说明一起删 / drop refinements that point at removed elements
        opf = re.sub(rf'\s*<meta\b[^>]*\brefines="#{re.escape(ident)}"[^>]*(?:/>|>.*?</meta>)', "", opf, flags=re.S)
    fresh = f"\n    <dc:title>{escape(entry['title'])}</dc:title>"
    if entry.get("author"):
        fresh += f"\n    <dc:creator>{escape(entry['author'])}</dc:creator>"
    if entry.get("year"):
        fresh += f"\n    <dc:date>{escape(entry['year'])}</dc:date>"
    opf, count = re.subn(r"(<(?:opf:)?metadata\b[^>]*>)", lambda m: m.group(1) + fresh, opf, count=1)
    if not count:
        raise ValueError("书里找不到 metadata 段")
    if "xmlns:dc=" not in opf:
        opf = re.sub(r"<((?:opf:)?metadata)\b", r'<\1 xmlns:dc="http://purl.org/dc/elements/1.1/"', opf, count=1)
    return opf


def write_book(source: Path, entry: dict, out_dir: Path) -> Path:
    target = out_dir / output_name(entry)
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(target, "w") as dst:
        container = src.read("META-INF/container.xml").decode("utf-8", "replace")
        opf_path = re.search(r'full-path="([^"]+)"', container).group(1)
        dst.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", zipfile.ZIP_STORED)
        seen = {"mimetype", COVER_PATH}
        cover_name = find_cover(src, opf_path, src.read(opf_path).decode("utf-8", "replace"))
        cover = make_cover(src.read(cover_name), Path(cover_name).suffix.lower()) if cover_name else None
        entry["cover"] = "有" if cover else ("封面图转不了" if cover_name else "书里没找到封面")
        if cover:
            # 不压缩，设备锁屏时直接读出来画。/ Stored uncompressed so the device reads and draws it directly.
            dst.writestr(zipfile.ZipInfo(COVER_PATH), cover, zipfile.ZIP_STORED)
        for info in src.infolist():
            if info.filename in seen or info.is_dir():
                continue
            seen.add(info.filename)
            data = src.read(info.filename)
            if info.filename == opf_path:
                data = rewrite_opf(data.decode("utf-8", "replace"), entry).encode("utf-8")
            # 图片本来就压过，原样存放，设备不用再解压一遍。/ Images are already compressed; store them so the device need not inflate.
            packed = info.filename.lower().endswith((".jpg", ".jpeg", ".png", ".gif", ".webp"))
            dst.writestr(zipfile.ZipInfo(info.filename), data, zipfile.ZIP_STORED if packed else zipfile.ZIP_DEFLATED)
    return target


# ---- 命令 / commands ----

def scan(folder: Path) -> list[dict]:
    books = sorted(p for p in folder.iterdir() if p.suffix.lower() == ".epub" and not p.name.startswith("."))
    plan = []
    for path in books:
        entry = propose(path)
        entry["source"] = str(path.resolve())
        plan.append(entry)
    return plan


def report(plan: list[dict]) -> None:
    for entry in plan:
        mark = "  " if entry["confidence"] == "high" else "请确认"
        print(f"{mark:<4} {entry['title']}  |  {entry.get('author', '')}  |  {entry.get('year', '')}")
        print(f"       原文件：{entry['file']}")
        for line in entry.get("why", []) + entry.get("problems", []):
            print(f"       · {line}")
    doubtful = sum(1 for e in plan if e["confidence"] != "high")
    print(f"\n共 {len(plan)} 本，其中 {doubtful} 本标了「请确认」。")


def apply(plan: list[dict], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    done = 0
    for entry in plan:
        if entry.get("skip") or not entry.get("title"):
            print(f"跳过：{entry['file']}")
            continue
        try:
            target = write_book(Path(entry["source"]), entry, out_dir)
            done += 1
            print(f"写好：{target.name}" + ("" if entry.get("cover") == "有" else f"（{entry.get('cover')}）"))
        except Exception as error:
            print(f"失败：{entry['file']}：{error}")
    print(f"\n{done} 本已写到 {out_dir}")


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "scan":
        parser = argparse.ArgumentParser(prog="prepare_books.py scan")
        parser.add_argument("folder", type=Path)
        parser.add_argument("-o", "--plan", type=Path, default=Path("plan.json"))
        opts = parser.parse_args(args[1:])
        plan = scan(opts.folder)
        opts.plan.parent.mkdir(parents=True, exist_ok=True)
        opts.plan.write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
        report(plan)
        print(f"清单写在 {opts.plan}。改好后运行：python3 tools/prepare_books.py apply {opts.plan} -o <输出文件夹>")
        return 0
    if args and args[0] == "apply":
        parser = argparse.ArgumentParser(prog="prepare_books.py apply")
        parser.add_argument("plan", type=Path)
        parser.add_argument("-o", "--out", type=Path, required=True)
        opts = parser.parse_args(args[1:])
        apply(json.loads(opts.plan.read_text(encoding="utf-8")), opts.out)
        return 0
    parser = argparse.ArgumentParser(description="整理 EPUB 的书名、作者、年份并重新打包 / tidy EPUB metadata and repack")
    parser.add_argument("folder", type=Path)
    parser.add_argument("-o", "--out", type=Path, required=True)
    opts = parser.parse_args(args)
    plan = scan(opts.folder)
    report(plan)
    apply(plan, opts.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
