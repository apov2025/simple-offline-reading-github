#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""把排版很差的中文 TXT 整理成阅读器能直接读的 EPUB。

Turn a badly formatted Chinese TXT into an EPUB the reader can open directly.

处理的问题 / What it fixes:
  - 固定宽度的硬换行：把被切开的句子接回成段落。
    Hard wraps at a fixed width: lines are joined back into paragraphs.
  - 古登堡计划等来源的英文版权头尾。
    Project Gutenberg style English header and footer.
  - 章回标题（第 X 回 / 章 / 卷 / 节），包括回目另起一行的写法。
    Chapter headings, including titles that continue on the next line.
  - 标点：用作句号的"．"、直引号、小号标点、"--"、汉字之间多余的空格。
    Punctuation: "．" used as a full stop, straight quotes, small-form marks, "--", stray spaces between Han characters.
  - 繁体转简体（需要 opencc：pip install opencc-python-reimplemented；没装就保持原样）。
    Traditional to Simplified (needs opencc; left as is when it is not installed).

输出文件名按阅读器的约定："书名 - 作者 (年份).epub"。
The output is named by the reader's convention: "<title> - <author> (<year>).epub".

用法 / Usage:
    python3 tools/txt_to_epub.py 红楼梦.txt --title 红楼梦 --author 曹雪芹 --year 1791 -o 输出文件夹
    python3 tools/txt_to_epub.py 某书.txt --title 某书 --keep-traditional --preview
"""
import argparse
import collections
import html
import os
import re
import sys
import uuid
import zipfile

CHAPTER = re.compile(r"^[\s\u3000]*(第[〇零一二三四五六七八九十百千两兩0-9０-９]+[回章卷节節部集])([\s\u3000:：]*)(.*)$")
SENTENCE_MARK = re.compile(r"[，。；！？,;!?]")
EXTRA_HEADING = re.compile(r"^[\s\u3000]*(楔子|引子|序言?|前言|后记|後記|尾声|尾聲|跋)[\s\u3000]*$")
RULE = re.compile(r"^[\s\u3000]*[-=─—_*·.]{4,}[\s\u3000]*$")
TERMINAL = "。！？…”』」）：；.!?"
HAN = r"㐀-鿿"


def decode(raw):
    for enc in ("utf-8-sig", "gb18030", "big5"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def strip_boilerplate(lines):
    """去掉古登堡式的英文头尾；没有就原样返回。/ Drop a Gutenberg-style header and footer, if any."""
    start = next((i for i, l in enumerate(lines) if l.startswith("*** START")), None)
    end = next((i for i, l in enumerate(lines) if l.startswith("*** END")), None)
    if start is not None:
        lines = lines[start + 1:end if end is not None and end > start else None]
    return lines


def indent_of(line):
    """行首缩进有几个字宽：全角空格算 1，半角空格算半个。/ Leading indent in ems."""
    width = 0.0
    for ch in line:
        if ch == "\u3000":
            width += 1
        elif ch in " \t":
            width += 0.5
        else:
            break
    return width


def join(a, b):
    """接两行：中文之间直接接，两边都是英文数字时补一个空格。/ Join two lines; add a space only between Latin text."""
    if a and b and a[-1].isascii() and a[-1].isalnum() and b[0].isascii() and b[0].isalnum():
        return a + " " + b
    return a + b


def clean(text):
    """段内的标点和空格。/ Punctuation and spacing inside a paragraph."""
    text = text.replace("\u3000", "").strip()
    text = re.sub(rf"(?<=[{HAN}，。、；：？！”’》」』）—…])[ \t]+(?=[{HAN}“‘《「『（，。、；：？！”’》」』）—…])", "", text)
    text = text.replace("．", "。").replace("--", "——").replace("─", "—")
    for small, full in zip("﹔﹕﹖﹗﹐﹑", "；：？！，、"):
        text = text.replace(small, full)
    text = re.sub(r"([：，；])\1+", r"\1", text)
    # 双引号（直的、弯的）按出现的先后重新配成一开一合：劣质文本里常把两个都写成开引号，
    # 或者干脆用直引号。中文的引号嵌套用的是单引号或「」，所以双引号不会套双引号。
    # Double quotes (straight or curly) are re-paired in order, open then close: poor texts often
    # write both as opening quotes or use straight ones. Chinese nests with single quotes or 「」,
    # so a double quote never sits inside another.
    parts = re.split('["“”]', text)
    if len(parts) > 1:
        text = "".join(p + ("“" if i % 2 == 0 else "”") for i, p in enumerate(parts[:-1])) + parts[-1]
    text = re.sub(rf"(?<=[{HAN}。，！？])'", "’", text)
    text = re.sub(rf"'(?=[{HAN}])", "‘", text)
    # 夹在汉字里的半角逗号、分号、问号、叹号换成全角。/ ASCII marks between Han characters become fullwidth.
    for half, full in ((",", "，"), (";", "；"), ("?", "？"), ("!", "！"), (":", "：")):
        text = re.sub(rf"(?<=[{HAN}”’》）]){re.escape(half)}(?=[{HAN}“‘《（]|$)", full, text)
    return text


def parse(lines):
    """把行切成章节，每章是 (标题, [(类型, 文字)])，类型是 'p' 或 'verse'。
    Split lines into chapters: (title, [(kind, text)]) with kind 'p' or 'verse'."""
    lengths = [len(l.rstrip()) for l in lines if len(l.strip()) >= 12]
    wrap = collections.Counter(lengths).most_common(1)[0][0] if lengths else 0
    marked = sum(1 for l in lines if l.strip() and indent_of(l) >= 1) + sum(1 for l in lines if not l.strip())
    # 既没有缩进也没有空行来分段时，只能靠"行短 + 句末标点"猜段落结尾。
    # With neither indents nor blank lines marking paragraphs, guess paragraph ends from "short line + closing mark".
    guess = marked < len(lines) * 0.02

    chapters, title, blocks, current = [], None, [], None
    joined = 0
    pending_title = False

    def flush():
        nonlocal current
        if current:
            text = clean(current)
            if text:
                blocks.append(("p", text))
        current = None

    def close_chapter():
        nonlocal title, blocks
        flush()
        if title is not None or blocks:
            chapters.append((title, blocks))
        title, blocks = None, []

    def is_heading(line, head):
        """"第 X 回"后面要么空着、要么隔着空格或冒号写标题；紧跟着字的，只有整行很短又不带句读才算
        （"第一章风起"是标题，"第四回中既将薛家母子……"是正文）。
        After "第 X 回" comes nothing, or a title set off by a space or colon; text running straight on
        counts only when the line is short and has no sentence marks."""
        if head is None or len(line.strip()) > 60:
            return False
        if not head.group(3) or head.group(2):
            return not SENTENCE_MARK.search(head.group(3)) or len(line.strip()) <= 40
        return len(line.strip()) <= 30 and not SENTENCE_MARK.search(head.group(3))

    for raw in lines:
        line = raw.rstrip()
        if not line.strip() or RULE.match(line):
            flush()
            continue
        head = CHAPTER.match(line)
        if is_heading(line, head) or EXTRA_HEADING.match(line):
            if head:
                rest = re.sub(r"[\s\u3000]+", " ", head.group(3)).strip()
                new_title = head.group(1) + (" " + rest if rest else "")
            else:
                rest = ""
                new_title = line.strip().replace("\u3000", "")
            # 有的文本把同一个标题连写两遍。/ Some texts repeat the same heading twice in a row.
            if title is not None and not blocks and current is None and new_title.split(" ")[0] == title.split(" ")[0]:
                if len(new_title) > len(title):
                    title = new_title
                pending_title = " " not in title
                continue
            close_chapter()
            title = new_title
            pending_title = head is not None and not rest
            continue
        if pending_title:
            # 回目另起一行（有的还隔着一条分隔线）：像"上半句 下半句"这样中间有空格、不带句读的一行并进标题。
            # The title sits on its own line (sometimes after a rule): a line shaped like "first half
            # second half", with a space in the middle and no sentence marks, joins the heading.
            pending_title = False
            bare = re.sub(r"[\s\u3000]+", " ", line).strip()
            if 10 <= len(bare) <= 40 and " " in bare and not re.search(r"[，。；：？！,.]", bare):
                title += " " + bare
                continue
        if title is None and not blocks and line.isascii():
            continue  # 正文开始前残留的英文元数据 / leftover ASCII metadata before the text starts
        depth = indent_of(line)
        text = line.strip()
        if depth >= 3:
            # 缩进三四个字的是诗词，一行一句，不和上下文接。/ Deeply indented lines are verse: one line each, never joined.
            flush()
            blocks.append(("verse", clean(text)))
            continue
        if depth >= 1 or current is None:
            flush()
            current = text
        else:
            current = join(current, text)
            joined += 1
        if guess and len(line) < wrap - 3 and text[-1] in TERMINAL:
            flush()
    close_chapter()
    return chapters, joined, wrap


XHTML = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="zh">
<head><title>{title}</title></head>
<body>
{body}
</body>
</html>
"""


def write_epub(path, title, author, year, chapters):
    book_id = "urn:uuid:" + str(uuid.uuid5(uuid.NAMESPACE_URL, f"{title}|{author}|{year}"))
    esc = html.escape
    items, spine, nav = [], [], []
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml",
                   '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                   '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
                   compress_type=zipfile.ZIP_DEFLATED)
        for i, (chapter_title, blocks) in enumerate(chapters, 1):
            name = f"c{i:04d}.xhtml"
            heading = chapter_title or title
            body = [f"<h2>{esc(heading)}</h2>"]
            body += [f'<p class="{kind}">{esc(text)}</p>' if kind == "verse" else f"<p>{esc(text)}</p>" for kind, text in blocks]
            z.writestr("OEBPS/" + name, XHTML.format(title=esc(heading), body="\n".join(body)), compress_type=zipfile.ZIP_DEFLATED)
            items.append(f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>')
            spine.append(f'<itemref idref="c{i}"/>')
            nav.append(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>{esc(heading)}</text></navLabel><content src="{name}"/></navPoint>')
        date = f"<dc:date>{esc(year)}</dc:date>" if year else ""
        creator = f"<dc:creator>{esc(author)}</dc:creator>" if author else ""
        z.writestr("OEBPS/content.opf",
                   '<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
                   f'<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{esc(title)}</dc:title>{creator}{date}'
                   f'<dc:language>zh</dc:language><dc:identifier id="id">{book_id}</dc:identifier></metadata>'
                   f'<manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>{"".join(items)}</manifest>'
                   f'<spine toc="ncx">{"".join(spine)}</spine></package>', compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx",
                   '<?xml version="1.0" encoding="utf-8"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                   f'<head><meta name="dtb:uid" content="{book_id}"/></head><docTitle><text>{esc(title)}</text></docTitle>'
                   f'<navMap>{"".join(nav)}</navMap></ncx>', compress_type=zipfile.ZIP_DEFLATED)


def file_stem(title, author, year):
    safe = lambda s: re.sub(r'[\\/:*?"<>|\x00-\x1f]', " ", s).strip()
    name = safe(title)
    if author:
        name += " - " + safe(author)
    if year:
        name += f" ({year})"
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("txt")
    ap.add_argument("--title", required=True, help="书名 / title")
    ap.add_argument("--author", default="", help="作者 / author")
    ap.add_argument("--year", default="", help="年份，四位数字 / four-digit year")
    ap.add_argument("-o", "--out", default=".", help="输出文件夹 / output folder")
    ap.add_argument("--keep-traditional", action="store_true", help="不转简体 / keep Traditional characters")
    ap.add_argument("--preview", action="store_true", help="多打印几段整理结果 / print more of the result")
    args = ap.parse_args()
    if args.year and not re.fullmatch(r"\d{4}", args.year):
        sys.exit("年份要写四位数字 / the year must be four digits")

    text = decode(open(args.txt, "rb").read()).replace("\r\n", "\n").replace("\r", "\n")
    if not args.keep_traditional:
        try:
            from opencc import OpenCC
            text = OpenCC("t2s").convert(text)
        except ImportError:
            print("没有安装 opencc，保持原文的繁简：pip install opencc-python-reimplemented", file=sys.stderr)
    lines = strip_boilerplate(text.split("\n"))
    chapters, joined, wrap = parse(lines)
    if not chapters:
        sys.exit("没有读出任何正文 / no text found")

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, file_stem(args.title, args.author, args.year) + ".epub")
    write_epub(path, args.title, args.author, args.year, chapters)

    paragraphs = sum(len(b) for _, b in chapters)
    verse = sum(1 for _, b in chapters for kind, _ in b if kind == "verse")
    chars = sum(len(t) for _, b in chapters for _, t in b)
    print(f"原文 {len(lines)} 行，常见行宽 {wrap} 字；接回 {joined} 处断行")
    print(f"整理成 {len(chapters)} 章、{paragraphs} 段（其中诗词 {verse} 行）、{chars} 字")
    print(f"已写入 {path}（{os.path.getsize(path) / 1024:.0f} KB）")
    untitled = sum(1 for t, _ in chapters if t is None)
    if untitled:
        print(f"注意：有 {untitled} 段正文在第一个章回标题之前，已单独成章")
    show = 6 if args.preview else 2
    for chapter_title, blocks in chapters[:2]:
        print(f"\n【{chapter_title or '（无标题）'}】")
        for kind, t in blocks[:show]:
            print(("    " if kind == "verse" else "  ") + (t if len(t) <= 110 else t[:110] + "…"))


if __name__ == "__main__":
    main()
