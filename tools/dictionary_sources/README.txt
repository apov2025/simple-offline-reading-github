Reviewed supplemental dictionary records

supplement.json contains per-entry provenance and original text for 56 explicit additions/replacements: 50 Unihan translations, 3 Wiktionary translations, and 3 direct Wiktionary Chinese definitions (bank, read, beautiful). They are not inferred from the displayed words. Every translation is marked 译释 in its runtime source label.

Wiktionary source revisions are fixed by oldid URLs and the original API responses in this folder; licenses: CC-BY-SA-4.0 for Wiktionary-derived records, Unicode-3.0 for Unihan-derived records. Do not label Unihan content as Wiktionary. Full source notices and hashes are shipped alongside the compiled offline.dict in dict/sources/README.txt and dict/manifest.json.

Build with Python venv + opencc-python-reimplemented==0.1.7:
python tools/build_simplified_dictionary.py raw.gz offline.dict --kfcd hyzd.txt --conversion-data tools/dictionary_conversion --unihan Unihan.zip --supplement tools/dictionary_sources/supplement.json

raw.gz must match the manifest SHA-256; it is a Kaikki Chinese Wiktionary snapshot. KFCD source https://github.com/kfcd/hyzd commit 04987755b16264636c01d28df4d96fa14dadd210, CC-BY-3.0. Unihan 17.0.0 from https://www.unicode.org/Public/17.0.0/ucd/Unihan.zip. OpenCC tables use ver.1.1.9, Apache-2.0. Conversion implementation https://github.com/yichen0831/opencc-python, version 0.1.7, Apache-2.0.

Run python tools/test_simplified_dictionary.py. The tests exercise actual table conversion (including stabilization after phrase substitution), source-empty records, traditional aliases, numeric-to-tone-mark pinyin, and pronunciation association for polyphonic records. No MOE dataset is consumed by this compiler.

Expansion simplified-2 adds source=HanziDict original definitions only from
https://github.com/sinobaba/hanzidict at 9bbe0b0cce38e8efa4dca74ea263268b9e6af895
(snapshot 2026-10-20, MIT). All zdic(汉典) rows are excluded, including from
the distributable original-only snapshot. Pass --hanzidict hanzidict-original-only.json.
Original upstream file SHA and filtering provenance are retained in its meta.selection.
Keep official character keys (e.g. 乾 and 干) distinct. Readings remain attached
to their own senses; at most 12 senses selected round-robin across readings.
