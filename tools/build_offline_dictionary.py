#!/usr/bin/env python3
"""从中文维基词典快照制作小纸 Pico 词库。 / Build an offline Chinese Wiktionary index."""
import argparse
from collections import Counter, defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import unicodedata
import zipfile

SOURCE = 'https://kaikki.org/zhwiktionary/raw-wiktextract-data.jsonl.gz'
HEADER = struct.Struct('<8s6I')
ROW = struct.Struct('<64sQII')
HAN = re.compile(r'[㐀-䶿一-鿿豈-﫿\U00020000-\U000323af]')
EN = re.compile(r"[A-Za-z]+(?:[ ’'\-][A-Za-z]+)*")
LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ’‘", "abcdefghijklmnopqrstuvwxyz''")


def normalize(word):
    return ' '.join(unicodedata.normalize('NFKC', word).translate(LOWER).split())


def valid(key, english):
    if not key or len(key.encode()) > 63 or any(ord(c) < 32 for c in key):
        return False
    if english:
        return EN.fullmatch(key) and (len(key) > 1 or key in ('a', 'i'))
    return all(HAN.fullmatch(c) for c in key)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def unicode_tables(path):
    simplified, traditional, readings, frequencies = {}, {}, {}, {}
    if path:
        with zipfile.ZipFile(path) as archive:
            for filename in ('Unihan_Readings.txt', 'Unihan_Variants.txt'):
                with archive.open(filename) as f:
                    for line in f:
                        if not line.startswith(b'U+'):
                            continue
                        code, field, value = line.decode().rstrip().split('\t')
                        char = chr(int(code[2:], 16))
                        if field == 'kMandarin':
                            readings[char] = value.split()
                        elif field == 'kHanyuPinlu':
                            frequencies[char] = sum(int(n) for n in re.findall(r'\((\d+)\)', value))
                        elif field in ('kSimplifiedVariant', 'kTraditionalVariant') and ' ' not in value:
                            table = simplified if field == 'kSimplifiedVariant' else traditional
                            table[ord(char)] = chr(int(value[2:], 16))
    return simplified, traditional, readings, frequencies


def build(source, output, unihan=None):
    entries, aliases = {}, defaultdict(list)
    translations = defaultdict(list)
    counts = Counter()
    with gzip.open(source, 'rt', encoding='utf-8') as f:
        for line in f:
            record = json.loads(line)
            if record.get('pos') == 'hard-redirect':
                src = normalize(record.get('title', ''))
                dest = normalize(record.get('redirect', '').split('#')[0])
                if src != dest and ((valid(src, False) and valid(dest, False)) or
                                    (valid(src, True) and valid(dest, True))):
                    if dest not in aliases[src]:
                        aliases[src].append(dest)
                continue
            lang = record.get('lang_code')
            if lang not in ('en', 'zh', 'cmn'):
                continue
            english = lang == 'en'
            word = record['word']
            key = normalize(word)
            if not valid(key, english):
                continue
            counts['input_' + lang] += 1
            if not english:
                for translation in record.get('translations', []):
                    if translation.get('lang_code') != 'en':
                        continue
                    target = normalize(translation.get('word', ''))
                    if valid(target, True) and word not in translations[target]:
                        translations[target].append(word)
                for form in record.get('forms', []):
                    if {'Simplified-Chinese', 'Traditional-Chinese'}.intersection(form.get('tags', [])):
                        variant = normalize(form['form'])
                        if valid(variant, False) and variant != key and key not in aliases[variant]:
                            aliases[variant].append(key)
            new = []
            pos = record.get('pos_title', '')
            if len(pos.encode()) >= 24:
                pos = ''
            for sense in record.get('senses', []):
                refs = sense.get('form_of', []) + sense.get('alt_of', [])
                if refs:
                    for ref in refs:
                        target = normalize(ref['word'])
                        if valid(target, english) and target != key and target not in aliases[key]:
                            aliases[key].append(target)
                    continue
                glosses = sense.get('glosses', [])
                gloss = '；'.join(dict.fromkeys(' '.join(g.split()) for g in glosses if g.strip()))
                if '__NOTITLECONVERT__' in gloss or '發音和釋義，請見' in gloss:
                    target = re.search(r'請見「([^」（]+)', gloss)
                    if target and valid(normalize(target[1]), False) and normalize(target[1]) != key:
                        if normalize(target[1]) not in aliases[key]:
                            aliases[key].append(normalize(target[1]))
                    continue
                if not HAN.search(gloss) or gloss == word or len(gloss.encode()) >= 768:
                    continue
                synonyms = []
                if english:
                    links = list(sense.get('synonyms', []))
                    links.extend(s for s in record.get('synonyms', []) if s.get('sense') in glosses)
                    for link in links:
                        name = link.get('word', '')
                        if EN.fullmatch(name) and normalize(name) != key and name not in synonyms:
                            if len(', '.join(synonyms + [name]).encode()) < 192:
                                synonyms.append(name)
                new.append({'pos': pos, 'gloss': gloss, 'synonyms': synonyms})
            if not new:
                continue
            entry = entries.setdefault(key, {'word': word, 'pronunciation': '', 'senses': []})
            for sound in record.get('sounds', []):
                if english:
                    pron = sound.get('ipa', '')
                elif {'Pinyin', 'Mandarin'}.issubset(sound.get('tags', [])):
                    pron = sound.get('zh_pron', '')
                    pron = re.sub(r' [(][A-Za-z¹²³⁴⁵ ]+[)]$', '', pron)
                else:
                    continue
                if english and entry['pronunciation']:
                    break
                parts = entry['pronunciation'].split(' · ') if entry['pronunciation'] else []
                if pron and pron not in parts and len(' · '.join(parts + [pron]).encode()) < 192:
                    entry['pronunciation'] = ' · '.join(parts + [pron])
                    break
            new.sort(key=lambda sense: bool(re.match(r'^音 [a-zA-Z\u00c0-\u024f].*亦作', sense['gloss'])))
            for s in new:
                if len(entry['senses']) >= 12:
                    break
                if s not in entry['senses']:
                    entry['senses'].append(s)

    def resolve(key, seen):
        if key in entries:
            return key
        if key in seen or len(seen) > 12:
            return None
        for target in aliases.get(key, ()):
            result = resolve(target, seen | {key})
            if result:
                return result
        return None

    keys = {k: k for k in entries}
    for key in aliases:
        resolved = resolve(key, set())
        if resolved:
            keys[key] = resolved
    simplified, traditional, readings, frequencies = unicode_tables(unihan)
    variants = defaultdict(set)
    for key, dest in keys.items():
        if not HAN.match(key):
            continue
        for table in (simplified, traditional):
            variant = key.translate(table)
            if variant != key and variant not in keys and valid(variant, False):
                variants[variant].add(dest)
    for variant, destinations in variants.items():
        if len(destinations) == 1:
            keys[variant] = next(iter(destinations))
            counts['unicode_alias_keys'] += 1
    for key, entry in entries.items():
        if key in readings:
            prons = list(readings[key])
            if entry['pronunciation']:
                prons.extend(entry['pronunciation'].split(' · '))
            pron = ' · '.join(dict.fromkeys(prons))
            if len(pron.encode()) < 192:
                entry['pronunciation'] = pron
    def usage_order(word):
        frequency = min(frequencies.get(c, 0) for c in word.translate(simplified))
        return (len(word) == 1, frequency == 0, -frequency,
                not all('一' <= c <= '鿿' for c in word))

    for key, words in translations.items():
        if key in keys:
            continue
        keys[key] = key
        words = sorted(words, key=usage_order)[:12]
        entries[key] = {'word': key, 'pronunciation': '', 'senses': [
            {'pos': '译词', 'gloss': w, 'synonyms': []} for w in words]}
        counts['reverse_translation_entries'] += 1
    offset = HEADER.size + len(keys) * ROW.size
    indexed = {}
    with output.open('w+b') as f:
        f.seek(offset)
        for key in sorted(entries):
            blob = json.dumps(entries[key], ensure_ascii=False, separators=(',', ':')).encode()
            if len(blob) > 16384:
                raise ValueError('oversize record ' + key)
            indexed[key] = (f.tell(), len(blob))
            f.write(blob)
        size = f.tell()
        f.seek(0)
        f.write(HEADER.pack(b'SORDICT1', 1, len(keys), HEADER.size, offset, size, 0))
        for key in sorted(keys):
            f.write(ROW.pack(key.encode(), *indexed[keys[key]], 0))
    counts.update(keys=len(keys), entries=len(entries), alias_keys=len(keys)-len(entries),
                  english_keys=sum(bool(EN.fullmatch(k)) for k in keys),
                  chinese_single_keys=sum(len(k)==1 and bool(HAN.fullmatch(k)) for k in keys),
                  entries_with_synonyms=sum(any(s['synonyms'] for s in e['senses']) for e in entries.values()))
    return {'format':'SORDICT1', 'source_url':SOURCE, 'source_sha256':digest(source),
            'unihan_url':'https://www.unicode.org/Public/17.0.0/ucd/Unihan.zip' if unihan else None,
            'unihan_sha256':digest(unihan) if unihan else None, 'file_sha256':digest(output), 'bytes':size, 'counts':dict(counts)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--unihan', type=Path)
    args = parser.parse_args()
    manifest = build(args.source, args.output, args.unihan)
    args.output.with_name('manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
