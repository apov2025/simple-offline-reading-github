"""逐条核对原始六列、全部读音和查询索引。 / Audit the supplement against all six source fields."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re

from build_moe_mini_dictionary import FIELDS, HEADER, ROW, SOURCE_LABEL, read_official_rows


def validate(source, dictionary):
    data = dictionary.read_bytes()
    magic, version, count, index, start, size, reserved = HEADER.unpack_from(data)
    assert (magic, version, index, start, size, reserved) == (
        b'SORDICT1', 1, 32, 32 + ROW.size * count, len(data), 0)
    rows = defaultdict(list)
    for values in read_official_rows(source):
        rows[values[0]].append(values)
    pattern = (r'單字　([^\n]*)\n部首　([^\n]*)　總筆畫數　([^\n]*)　部首外筆畫　([^\n]*)'
               r'\n注音　([^\n]*)\n解釋\n(.*?)(?=\n\n單字　|\Z)')
    keys, entries = {}, {}
    previous = b''
    for i in range(count):
        raw, position, length, zero = ROW.unpack_from(data, index+i*ROW.size)
        key = raw.rstrip(b'\0')
        assert previous < key and raw == key.ljust(64, b'\0')
        assert not zero and start <= position < position+length <= size and length <= 16384
        previous = key
        keys[key.decode()] = (position, length)
        if position in entries:
            continue
        obj = json.loads(data[position:position+length])
        assert obj['source'] == SOURCE_LABEL and 0 < len(obj['full_text'].encode()) <= 8191
        original = rows[obj['word']]
        decoded = re.findall(pattern, obj['full_text'], re.S)
        assert decoded == original, (obj['word'], FIELDS)
        entries[position] = obj['word']
    heads = set(entries.values())
    excluded = set(rows) - heads
    assert excluded == {'?', '汨', '硓'}
    for word in heads:
        assert entries[keys[word][0]] == word
    for query, head in [('走','走'), ('这','這'), ('问','問'), ('道','道')]:
        assert entries[keys[query][0]] == head
    return (len(keys), len(heads), sum(len(rows[word]) for word in heads))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('official_zip', type=Path)
    parser.add_argument('dictionary', type=Path)
    args = parser.parse_args()
    print('Verified query keys, distinct heads, complete official rows:',
          *validate(args.official_zip, args.dictionary))
