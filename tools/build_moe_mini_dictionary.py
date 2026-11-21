#!/usr/bin/env python3
"""制作教育部《國語小字典》的独立无损索引。 / Index the separate, unabridged MOE text."""
import argparse
from collections import defaultdict
from io import BytesIO
import json
from pathlib import Path
import re
from tempfile import NamedTemporaryFile
import xml.etree.ElementTree as ET
import zipfile

from build_offline_dictionary import HEADER, ROW, digest, unicode_tables

VERSION = '2019_20260929'
SOURCE_LABEL = '教育部《國語小字典》'
SOURCE_URL = ('https://language.moe.gov.tw/001/Upload/Files/site_content/M0001/'
              f'respub/download/dict_mini_{VERSION}.zip')
FIELDS = ('單字', '部首', '總筆畫數', '部首外筆畫', '注音', '解釋')
GLYPH = re.compile(r'\.(?:gif|png|jpe?g|svg|webp|bmp)\b', re.I)


def read_official_rows(source):
    with zipfile.ZipFile(source) as archive:
        xlsx = archive.read(f'dict_mini_{VERSION}.xlsx')
    with zipfile.ZipFile(BytesIO(xlsx)) as workbook:
        strings = ET.fromstring(workbook.read('xl/sharedStrings.xml'))
        text = [''.join(t.text or '' for t in s.findall('.//{*}t')) for s in strings]
        sheet = ET.fromstring(workbook.read('xl/worksheets/sheet1.xml'))
    for row in sheet.findall('.//{*}row'):
        cells = {}
        for cell in row.findall('{*}c'):
            value = cell.findtext('{*}v')
            if value is None:
                continue
            column = re.sub(r'\d+$', '', cell.attrib['r'])
            cells[column] = text[int(value)] if cell.get('t') == 's' else value
        if not cells:
            continue
        if set(cells) != set('ABCDEF'):
            raise ValueError(f'Unexpected field count at spreadsheet row {row.get("r")}')
        values = tuple(cells[col] for col in 'ABCDEF')
        if row.get('r') == '1':
            if values != FIELDS:
                raise ValueError('Unexpected official field names')
            continue
        yield values


def display_row(row):
    head, radical, strokes, other, sound, definition = row
    return (f'單字　{head}\n部首　{radical}　總筆畫數　{strokes}　部首外筆畫　{other}\n'
            f'注音　{sound}\n解釋\n{definition}')


def build(source, unihan, output):
    heads = defaultdict(list)
    for row in read_official_rows(source):
        heads[row[0]].append(row)
    excluded = [word for word, rows in heads.items()
                if word == '?' or any(GLYPH.search(value) for row in rows for value in row)]
    excluded_rows = sum(len(heads[word]) for word in excluded)
    for word in excluded:
        del heads[word]
    simplified, _, _, _ = unicode_tables(unihan)
    suggestions = defaultdict(set)
    for word in heads:
        variant = word.translate(simplified)
        if variant != word and variant not in heads:
            suggestions[variant].add(word)
    keys = {key: key for key in heads}
    keys.update({key: next(iter(candidates)) for key, candidates in suggestions.items()
                 if len(candidates) == 1})

    offsets = {}
    max_text, max_blob = 0, 0
    start = HEADER.size + ROW.size * len(keys)
    with NamedTemporaryFile('w+b', dir=output.parent, delete=False) as binary:
        temporary = Path(binary.name)
        try:
            binary.seek(start)
            for word, rows in sorted(heads.items()):
                full_text = '\n\n'.join(display_row(row) for row in rows)
                record = {'word': word, 'source': SOURCE_LABEL, 'full_text': full_text}
                blob = json.dumps(record, ensure_ascii=False, separators=(',', ':')).encode()
                max_text = max(max_text, len(full_text.encode()))
                max_blob = max(max_blob, len(blob))
                if max_text > 8191 or max_blob > 16384:
                    raise ValueError(f'Full entry exceeds the format limit: {word}')
                offsets[word] = (binary.tell(), len(blob))
                binary.write(blob)
            size = binary.tell()
            binary.seek(0)
            binary.write(HEADER.pack(b'SORDICT1', 1, len(keys), HEADER.size, start, size, 0))
            for key, word in sorted(keys.items()):
                raw = key.encode()
                if not raw or len(raw) > 63:
                    raise ValueError(f'Invalid key length: {key}')
                binary.write(ROW.pack(raw, *offsets[word], 0))
            binary.flush()
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)

    return {'format': 'SORDICT1', 'source': SOURCE_LABEL, 'source_url': SOURCE_URL,
            'version': VERSION, 'official_zip_sha256': digest(source),
            'unihan_sha256': digest(unihan), 'license': 'CC BY-ND 3.0 TW',
            'keys': len(keys), 'heads': len(heads), 'aliases': len(keys)-len(heads),
            'included_official_rows': sum(map(len, heads.values())),
            'excluded_whole_heads': sorted(excluded), 'excluded_official_rows': excluded_rows,
            'ambiguous_aliases_skipped': sum(len(v) > 1 for v in suggestions.values()),
            'maximum_full_text_bytes': max_text, 'maximum_json_bytes': max_blob,
            'bytes': size, 'sha256': digest(output)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_zip', type=Path)
    parser.add_argument('unihan_zip', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    manifest = build(args.source_zip, args.unihan_zip, args.output)
    args.output.with_suffix('.manifest.json').write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
