import gzip
import os
import json
from pathlib import Path
import tempfile
import unittest
from build_simplified_dictionary import build, load_converter, tone_pinyin, HEADER, ROW

DATA = Path(os.environ.get("OPENCC_DATA_DIR", Path(__file__).parent / "dictionary_conversion"))
class SimplifiedTest(unittest.TestCase):
    def test_conversion_reaches_stable_simplified_form(self):
        c=load_converter(DATA)
        self.assertEqual(c("等同於單數"), "等同于单数")

    def test_tones(self):
        self.assertEqual(tone_pinyin('lv4/se4/xing2/hang5'), 'lǜ/sè/xíng/hang')
        self.assertEqual(tone_pinyin('liu2 gui3 ou3'), 'liú guǐ ǒu')

    def test_hanzidict_original_senses_and_standard_character_identity(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td); raw=p/'raw.gz'; kf=p/'hyzd.txt'; hd=p/'hanzidict.json'; out=p/'offline.dict'
            with gzip.open(raw,'wt') as f: f.write('')
            kf.write_text('')
            hd.write_text(json.dumps({'meta':{'snapshot':'test'},'characters':[
                {'char':'干','readings':[{'pinyin':'gān','senses':[{'source':'HanziDict','content':'乾燥'}]}]},
                {'char':'乾','readings':[{'pinyin':'qián','senses':[{'source':'HanziDict','content':'八卦之一，代表天'}]}]},
                {'char':'发','aliases':['發','髮'],'readings':[
                    {'pinyin':'fā','senses':[{'source':'HanziDict','content':'發出'},{'source':'HanziDict','content':'產生'}]},
                    {'pinyin':'fà','senses':[{'source':'HanziDict','content':'頭髮'}]}]},
                {'char':'㽏','senses':[{'source':'HanziDict','content':'味甜'}]},
                {'char':'先','readings':[{'pinyin':'xiān','senses':[
                    {'source':'zdic(汉典)','content':'不應匯入的第三方釋義'},
                    {'source':'HanziDict','content':'時間或次序在前'}]},
                    {'pinyin':'先','senses':[{'source':'HanziDict','content':'错误读音组应排除'}]}]},
                {'char':'空','readings':[{'pinyin':'kōng','senses':[{'source':'Other','content':'未知來源'}]}]}
            ]},ensure_ascii=False))
            manifest=build(raw,out,kf,DATA,hanzidict=hd)
            b=out.read_bytes();keys={}
            for i in range(HEADER.unpack_from(b)[2]):
                k,start,n,_=ROW.unpack_from(b,32+80*i);keys[k.rstrip(b'\0').decode()]=json.loads(b[start:start+n])
            self.assertEqual(keys['干']['pronunciation'],'gān')
            self.assertEqual(keys['乾']['pronunciation'],'qián')
            self.assertEqual(keys['乾']['word'],'乾')
            self.assertEqual(keys['发'],keys['髮'])
            self.assertEqual([s['pronunciation'] for s in keys['发']['senses']][:2],['fā','fà'])
            self.assertEqual(keys['先']['senses'][0]['gloss'],'时间或次序在前')
            self.assertEqual(len(keys['先']['senses']),1)
            self.assertEqual(keys['㽏']['senses'][0]['gloss'],'味甜')
            self.assertNotIn('空',keys)
            self.assertEqual(manifest['hanzidict']['included_source'],'HanziDict')
            self.assertEqual(manifest['hanzidict']['entries'],5)

    def test_source_meanings_polyphones_aliases_and_missing(self):
        records = [
          {'word':'銀行','lang_code':'zh','senses':[{'glosses':['辦理存款的機構']}],
           'sounds':[{'zh_pron':'yínháng','tags':['Mandarin','Pinyin']}]},
          {'word':'quiet','lang_code':'en','pos_title':'形容詞','senses':[{'glosses':['安靜、寧靜'],'synonyms':[{'word':'silent'}]}]},
          {'word':'行','lang_code':'zh','senses':[{'glosses':['無關舊義']}],
           'sounds':[{'zh_pron':'xíng','tags':['Mandarin','Pinyin']}]},
          {'word':'走','lang_code':'zh','senses':[{'tags':['no-gloss']}]},
          {'word':'重','lang_code':'zh','senses':[{'glosses':['重量或重复']}],
           'sounds':[{'zh_pron':'zhòng','tags':['Mandarin','Pinyin']},{'zh_pron':'chóng','tags':['Mandarin','Pinyin']}]}]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td); raw=p/'raw.gz'; kf=p/'hyzd.txt'; out=p/'offline.dict'
            with gzip.open(raw,'wt') as f:
                for r in records: f.write(json.dumps(r)+'\n')
            kf.write_text('行\t行\thang2\t銀行\t職業\t\n行\t行\txing2\t行走\t走\t\n走\t走\tzou3\t走路\t\t\n')
            manifest=build(raw,out,kf,DATA)
            self.assertEqual(build(raw,out,kf,DATA)['file_sha256'],manifest['file_sha256'])
            b=out.read_bytes(); keys={}
            for i in range(HEADER.unpack_from(b)[2]):
                k,start,n,_=ROW.unpack_from(b,32+80*i); keys[k.rstrip(b'\0').decode()]=json.loads(b[start:start+n])
            self.assertEqual(keys['銀行'],keys['银行'])
            self.assertEqual(keys['银行']['word'],'银行')
            self.assertEqual(keys['银行']['senses'][0]['gloss'],'办理存款的机构')
            self.assertEqual(keys['quiet']['senses'][0]['gloss'],'安静、宁静')
            self.assertEqual(keys['quiet']['senses'][0]['pos'],'形容词')
            self.assertEqual(keys['行']['source'],'开放汉语字典')
            self.assertEqual([(s['pronunciation'],s['gloss']) for s in keys['行']['senses']], [('háng','职业'),('xíng','走')])
            self.assertNotIn('走',keys)
            self.assertNotIn('pronunciation', keys['重']['senses'][0])
            self.assertEqual(keys['重']['pronunciation'], 'zhòng · chóng')
            self.assertNotIn('full_text',str(keys))

if __name__=='__main__': unittest.main()
