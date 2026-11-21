#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：目录层级、锚点和损坏回退夹具。/ English: TOC hierarchy, anchor and broken-navigation fixtures."""
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BODY = (
    '<html><head><title>ignored</title><script>ignored</script></head><body>'
    '<h1 id="start">序 &amp; A</h1> <h2 id="节&amp;二">第二节</h2>'
    '<p>X <a name="inline"/>Y 中</p><img id="pic" src="img.png"/>'
    '<p>Z</p><a id="end"/></body></html>'
)
LINKS = [
    ('父 &amp; 章', 'one.xhtml#start'),
    ('第二节', 'one.xhtml#%E8%8A%82%26%E4%BA%8C'),
    ('Inline', 'one.xhtml#inline'),
    ('Image', 'one.xhtml#pic'),
    ('Missing anchor', 'one.xhtml#absent'),
    ('Next', 'two.xhtml#next'),
    ('EOF', 'one.xhtml#end'),
]
SOURCES = [
    'tools/book_toc_host_test.c', 'main/book/book_source.c',
    'main/book/book_txt.c', 'main/book/gbk.c', 'main/book/book_epub.c',
    'tools/book_toc_test_support.c', 'main/book/html_text.c',
    'main/book/book_image.c', 'main/book/vendor/tjpgd.c',
]


def ncx_point(index):
    title, href = LINKS[index]
    children = ''.join(ncx_point(j) for j in (1, 2, 3)) if index == 0 else ''
    return (f'<navPoint><navLabel><text>{title}</text></navLabel>'
            f'<content src="{href}"/>{children}</navPoint>')


def nav_link(index):
    title, href = LINKS[index]
    children = '<ol>' + ''.join(nav_link(j) for j in (1, 2, 3)) + '</ol>' if index == 0 else ''
    return f'<li><a href="{href}"><span>{title}</span></a>{children}</li>'


def navigation(links):
    return '<html><nav epub:type="toc">' + links + '</nav></html>'


def write_book(path, name, toc):
    ncx = name.startswith('ncx')
    media = 'application/x-dtbncx+xml' if ncx else 'application/xhtml+xml'
    properties = '' if ncx else 'nav'
    opf = (
        '<package><manifest>'
        '<item id="one" href="one.xhtml" media-type="application/xhtml+xml"/>'
        '<item id="two" href="two.xhtml" media-type="application/xhtml+xml"/>'
        f'<item id="toc" href="toc.xml" media-type="{media}" properties="{properties}"/>'
        '</manifest><spine toc="toc"><itemref idref="one"/>'
        '<itemref idref="two"/></spine></package>'
    )
    second = ('<p id="next">Next<p title="' if name == 'corrupt_target'
              else '<img src="img.png"/><p id="next">Next</p>')
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('META-INF/container.xml', '<container><rootfile full-path="book.opf"/></container>')
        archive.writestr('book.opf', opf)
        archive.writestr('one.xhtml', BODY)
        archive.writestr('two.xhtml', second)
        archive.writestr('img.png', b'placeholder-only')
        if toc:
            archive.writestr('toc.xml', toc)


def write_current_book(path, mode):
    body = '<p id="first"><a name="same"/>A</p><p id="second">B</p><img src="img.png"/>'
    toc = ('<ol><li><a href="c1.xhtml#first">Parent</a><ol>'
           '<li><a href="c1.xhtml#same">Deep first</a></li>'
           '<li><a href="c1.xhtml#first">Duplicate</a></li></ol></li>'
           '<li><a href="c1.xhtml#missing">Missing</a></li>'
           '<li><a href="c1.xhtml#second">Second</a></li>'
           '<li><a href="c1.xhtml">Start</a></li>'
           '<li><a href="c3.xhtml#last">Last</a></li></ol>')
    if mode == 'allmissing':
        toc = '<a href="c1.xhtml#missing">Missing</a>'
    elif mode == 'many':
        body = ''.join(f'<p id="s{i}">x</p>' for i in range(3000))
        toc = ''.join(f'<a href="c1.xhtml#s{i}">{i}</a>' for i in range(3000))
    opf = ('<package><manifest>' + ''.join(
        f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(4)) +
        '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>' +
        '</manifest><spine>' + ''.join(f'<itemref idref="c{i}"/>' for i in range(4)) + '</spine></package>')
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('META-INF/container.xml', '<container><rootfile full-path="book.opf"/></container>')
        archive.writestr('book.opf', opf)
        archive.writestr('nav.xhtml', navigation(toc))
        for i in range(4):
            archive.writestr(f'c{i}.xhtml', body if i == 1 else '<img src="img.png"/><p id="last">Z</p>')
        archive.writestr('img.png', b'placeholder-only')


def main():
    ncx = '<ncx><navMap>' + ''.join(ncx_point(i) for i in (0, 4, 5, 6)) + '</navMap></ncx>'
    nav = navigation('<ol>' + ''.join(nav_link(i) for i in (0, 4, 5, 6)) + '</ol>')
    nav = nav.replace('<html>', '<html><nav epub:type="landmarks"><a href="one.xhtml">WRONG</a></nav>')
    unsafe = navigation(''.join(f'<a href="{href}">Invalid</a>' for href in (
        'https://example.com/a', '../escape.xhtml', 'missing.xhtml',
        'one.xhtml#%XX', 'one.xhtml#%00', 'one.xhtml#%FF',
    )))
    cases = [
        ('ncx', ncx, False),
        ('nav', nav, False),
        ('ncx_scope', '<ncx>' + ncx_point(5) + '</ncx>', True),
        ('ncx_wrongroot', ncx.replace('<ncx>', '<wrong>').replace('</ncx>', '</wrong>'), True),
        ('longtitle', nav.replace('父 &amp; 章', '中' * 100), False),
        ('corrupt_target', nav, False),
        ('entry_limit', navigation('<a href="one.xhtml">X</a>' * 32769), True),
        ('label_budget', navigation(('<a href="one.xhtml">' + '中' * 53 + '</a>') * 6600), True),
        ('broken', nav + '<broken>', True),
        ('missing', '', True),
        ('unsafe', unsafe, True),
        ('deep', navigation('<ol>' * 65 + 'x' + '</ol>' * 65), True),
    ]
    with tempfile.TemporaryDirectory(prefix='book-toc-') as temp:
        work = Path(temp)
        exe = work / 'toc'
        flags = ['-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                 '-g', '-fsanitize=address,undefined', '-DBOOK_HEAP_TRACK']
        includes = ['-I' + str(ROOT / 'tools/book_epub_stubs'),
                    '-I' + str(ROOT / 'tools/zip_host_stubs'), '-I' + str(ROOT / 'main/book')]
        zip_object = work / 'zip.o'
        subprocess.run(['cc', *flags, *includes, '-Dzip_extract=book_test_zip_extract_real',
                        '-c', str(ROOT / 'main/book/zip_reader.c'), '-o', str(zip_object)], check=True)
        subprocess.run([
            'cc', '-DBOOK_HEAP_TRACK', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
            '-g', '-fsanitize=address,undefined',
            '-I' + str(ROOT / 'tools/book_epub_stubs'),
            '-I' + str(ROOT / 'tools/zip_host_stubs'), '-I' + str(ROOT / 'main/book'),
            *[str(ROOT / source) for source in SOURCES], str(zip_object), '-lz', '-o', str(exe),
        ], check=True)
        for mode in ('advanced', 'allmissing', 'many'):
            path = work / (mode + '.epub')
            write_current_book(path, mode)
            print(f'TOC fixture: {mode}', flush=True)
            subprocess.run([str(exe), str(path), mode], check=True)
        for name, toc, fallback in cases:
            path = work / (name + '.epub')
            write_book(path, name, toc)
            print(f'TOC fixture: {name}', flush=True)
            subprocess.run([str(exe), str(path), 'fallback' if fallback else name], check=True)


if __name__ == '__main__':
    main()
