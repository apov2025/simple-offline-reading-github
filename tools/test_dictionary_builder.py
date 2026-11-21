"""生成器边界与来源关系。 / Verify builder boundaries and source relationships."""
import gzip
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from build_offline_dictionary import build, HEADER, ROW


class BuilderTest(unittest.TestCase):
    def test_source_links_and_order(self):
        records = [
            {'word':'銀行','lang_code':'zh','forms':[{'form':'银行','tags':['Simplified-Chinese']}],
             'senses':[{'glosses':['金融机构']}]},
            {'word':'bank','lang_code':'en','senses':[{'glosses':['河岸']},{'glosses':['银行']}],
             'synonyms':[{'word':'shore','sense':'河岸'},{'word':'junk'}]},
            {'word':'banked','lang_code':'en','senses':[{'form_of':[{'word':'bank'}]}]},
            {'word':'bogus','lang_code':'en','senses':[{'form_of':[{'word':'bogus'}]}]}]
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'input.gz'
            target = Path(folder) / 'offline.dict'
            with gzip.open(source, 'wt', encoding='utf-8') as f:
                for r in records:
                    f.write(json.dumps(r)+'\n')
            build(source, target)
            data = target.read_bytes()
            magic, version, count, index, offset, size, reserved = HEADER.unpack_from(data)
            self.assertEqual((magic,version,index,offset,size,reserved),(b'SORDICT1',1,32,32+80*4,len(data),0))
            rows = {}
            for n in range(count):
                raw, start, length, reserved = ROW.unpack_from(data, index+n*80)
                self.assertEqual(reserved,0)
                rows[raw.rstrip(b'\0').decode()] = json.loads(data[start:start+length])
            self.assertEqual(list(rows),sorted(['銀行','银行','bank','banked']))
            self.assertEqual(rows['银行'],rows['銀行'])
            self.assertEqual(rows['banked'],rows['bank'])
            self.assertEqual([s['synonyms'] for s in rows['bank']['senses']],[['shore'],[]])

    def test_redirects_unicode_and_translation_provenance(self):
        records = [
            {'word':'龍','lang_code':'zh','senses':[{'glosses':['神話動物']}]},
            {'word':'马','lang_code':'zh','senses':[{'glosses':[
                '__NOTITLECONVERT__ 關於「马」的發音和釋義，請見「馬」。']}]},
            {'word':'馬','lang_code':'zh','senses':[{'glosses':['一種動物']}]},
            {'title':'飛馬','redirect':'馬','pos':'hard-redirect'},
            {'word':'清美','lang_code':'zh','senses':[{'tags':['no-gloss']}],
             'translations':[{'lang_code':'en','word':'beautiful'}]},
            {'word':'美','lang_code':'zh','translations':[{'lang_code':'en','word':'beautiful'}]},
            {'word':'gone','lang_code':'en','senses':[{'tags':['no-gloss']}]},
            {'word':'ＨＡＰＰＹ','lang_code':'en','senses':[{'glosses':['快樂']}]}]
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source, target, unihan = folder/'input.gz', folder/'out.dict', folder/'u.zip'
            with gzip.open(source, 'wt', encoding='utf-8') as f:
                for record in records:
                    f.write(json.dumps(record)+'\n')
            with zipfile.ZipFile(unihan, 'w') as f:
                f.writestr('Unihan_Variants.txt','U+9F8D\tkSimplifiedVariant\tU+9F99\n')
                f.writestr('Unihan_Readings.txt','U+9F8D\tkMandarin\tlóng\n')
            first = build(source, target, unihan)
            self.assertEqual(build(source, target, unihan), first)
            data = target.read_bytes()
            keys = {}
            for n in range(HEADER.unpack_from(data)[2]):
                raw, pos, size, _ = ROW.unpack_from(data,32+n*80)
                keys[raw.rstrip(b'\0').decode()] = json.loads(data[pos:pos+size])
            self.assertEqual(keys['龙']['pronunciation'],'lóng')
            self.assertEqual(keys['飛馬'],keys['马'])
            self.assertEqual(keys['马']['senses'][0]['gloss'],'一種動物')
            self.assertEqual(keys['beautiful']['senses'][0],{'pos':'译词','gloss':'清美','synonyms':[]})
            self.assertNotIn('gone', keys)
            self.assertIn('happy', keys)


if __name__ == '__main__':
    unittest.main()
