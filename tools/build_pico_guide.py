#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""从小纸说明书源生成确定性的 EPUB。/ Build the Pico guide as a deterministic EPUB."""
from pathlib import Path
from html import escape
import argparse
import re
import zipfile
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def build(source: Path, target: Path):
    text = source.read_text(encoding='utf-8').strip()
    lines = text.splitlines()
    if not lines or not lines[0].startswith('# '):
        raise ValueError('Expected one document title')
    title = lines[0][2:].strip()
    chapters = [(title, [])]
    for block in re.split(r'\n\s*\n', '\n'.join(lines[1:]).strip()):
        if block.startswith('## '):
            chapters.append((block[3:].strip(), []))
        else:
            if block.startswith('#'):
                raise ValueError('Unsupported heading')
            chapters[-1][1].append(' '.join(block.splitlines()))
    if len(chapters) < 2 or any(not paragraphs for _, paragraphs in chapters):
        raise ValueError('Every chapter must have readable text')
    uid = 'urn:offline-reading:pico-guide'
    def xhtml(heading, content):
        return ('<?xml version="1.0" encoding="utf-8"?>\n'
                '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="zh-CN" xml:lang="zh-CN">'
                '<head><title>'+escape(heading)+'</title><link rel="stylesheet" href="style.css" type="text/css"/></head><body>'+content+'</body></html>').encode()
    documents = {}
    for index, (heading, paragraphs) in enumerate(chapters):
        documents[f'EPUB/ch{index:02}.xhtml'] = xhtml(heading, '<h1>'+escape(heading)+'</h1>'+''.join('<p>'+escape(p)+'</p>' for p in paragraphs))
    navigation = ''.join(f'<li><a href="ch{i:02}.xhtml">{escape(h)}</a></li>' for i,(h,_) in enumerate(chapters))
    documents['EPUB/nav.xhtml'] = xhtml('目录','<nav epub:type="toc" id="toc"><h1>目录</h1><ol>'+navigation+'</ol></nav>')
    navpoints=''.join(f'<navPoint id="n{i}" playOrder="{i+1}"><navLabel><text>{escape(h)}</text></navLabel><content src="ch{i:02}.xhtml"/></navPoint>' for i,(h,_) in enumerate(chapters))
    documents['EPUB/toc.ncx'] = ('<?xml version="1.0" encoding="utf-8"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="'+uid+'"/></head><docTitle><text>'+escape(title)+'</text></docTitle><navMap>'+navpoints+'</navMap></ncx>').encode()
    manifest=''.join(f'<item id="c{i}" href="ch{i:02}.xhtml" media-type="application/xhtml+xml"/>' for i in range(len(chapters)))
    spine=''.join(f'<itemref idref="c{i}"/>' for i in range(len(chapters)))
    documents['EPUB/content.opf'] = ('<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="uid">'+uid+'</dc:identifier><dc:title>'+escape(title)+'</dc:title><dc:creator>apov</dc:creator><dc:language>zh-CN</dc:language><dc:date>2026-11-21</dc:date><meta property="dcterms:modified">2026-11-21T00:00:00Z</meta></metadata><manifest>'+manifest+'<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/><item id="css" href="style.css" media-type="text/css"/></manifest><spine toc="ncx">'+spine+'</spine></package>').encode()
    documents['EPUB/style.css'] = b'body{margin:0 4%;line-height:1.65}h1{font-size:1.3em;line-height:1.4}p{margin:0 0 0.8em;text-indent:0}'
    documents['META-INF/container.xml'] = b'<?xml version="1.0" encoding="utf-8"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="EPUB/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
    for name, data in documents.items():
        if name.endswith(('.xhtml','.ncx','.opf','.xml')):
            ET.fromstring(data)
    target.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(target,'w') as z:
        for name, data in [('mimetype',b'application/epub+zip'),*sorted(documents.items())]:
            info=zipfile.ZipInfo(name,(1980,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_STORED if name=='mimetype' else zipfile.ZIP_DEFLATED
            info.create_system=0
            z.writestr(info,data)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert z.infolist()[0].filename=='mimetype' and z.infolist()[0].compress_type==zipfile.ZIP_STORED
    print(f'{len(chapters)} chapters, {target.stat().st_size} bytes')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'docs/pico-guide.md')
    parser.add_argument('--output',type=Path,default=ROOT/'builtin/pico/manual/pico-guide.epub')
    args=parser.parse_args()
    build(args.source,args.output)
