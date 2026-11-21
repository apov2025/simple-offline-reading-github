import json, tempfile, unittest
from pathlib import Path
from build_english_dictionary import read_dict,write_dict,augment,normalize,encode

def entry(word,gloss,source='维基词典'):
 return encode(dict(word=word,pronunciation='',source=source,senses=[dict(pos='',gloss=gloss,synonyms=[])]))
def row(word,translation,exchange='',phonetic=''):
 return dict(word=word,translation=translation,exchange=exchange,phonetic=phonetic,bnc='',frq='')
class EnglishTest(unittest.TestCase):
 def test_merge_preserves_chinese_bytes_and_lexical_forms(self):
  old={'中':entry('中','中间'),'go':entry('go','围旗'),'saw':entry('saw','锯子'),'walked':entry('walked','walk的过去式')}
  rows=[row('go','vi. 去, 走','p:went'),row('went','go的过去式','0:go/1:p'),row('walk','v. 步行','p:walked'),row('walked','walk的过去式','0:walk/1:p'),row('see','v. 看见','p:saw'),row('saw','n. 锯子\\nsee的过去式','0:see/1:p')]
  result,audit=augment(old,rows)
  self.assertEqual(result['中'],old['中']); self.assertEqual(result['went'],result['go']);self.assertEqual(result['walked'],result['walk'])
  self.assertIn('去',json.loads(result['go'])['senses'][0]['gloss']);self.assertEqual(json.loads(result['saw'])['word'],'saw')
 def test_conflicts_do_not_guess_and_contractions_remain(self):
  rows=[row('do','v. 做','3:does'),row('doe','n. 雌鹿','s:does'),row('does','do的第三人称单数形式','0:doe/1:s'),row('ax','n. 斧头','s:axes'),row('axe','n. 斧头','3:axes'),row("it's",'it is的缩写'),row('news','n. 新闻')]
  result,audit=augment({},rows)
  self.assertEqual(result['does'],result['do']);self.assertNotIn('axes',result);self.assertEqual(json.loads(result["it's"])['word'],"it's");self.assertIn('news',result)
 def test_lexical_forms_show_lemma_and_keep_own_meaning(self):
  rows=[row('study','v. 学习','p:studied'),row('studied','a. 有计划的','0:study/1:p'),row('combine','v. 结合','i:combining'),row('combining','a. 结合性的','0:combine/1:i'),row('circulation','n. 流通')]
  old={'circulation':entry('circulation','︁循环')}
  result,_=augment(old,rows)
  studied=json.loads(result['studied'])
  self.assertEqual(studied['word'],'studied');self.assertIn('study 的过去式；学习',studied['senses'][1]['gloss']);self.assertTrue(any('有计划的' in s['gloss'] for s in studied['senses']))
  self.assertEqual(result['combining'],result['combine']);self.assertNotIn('︁',result['circulation'].decode())
 def test_existing_alias_does_not_discard_source_lexical_gloss(self):
  old={'walk':entry('walk','步行'),'walks':entry('walk','步行')}
  result,_=augment(old,[row('walk','v. 步行','3:walks'),row('walks','n. 跳板','0:walk/1:s3')])
  e=json.loads(result['walks']);self.assertEqual(e['word'],'walks');self.assertTrue(any('跳板' in v['gloss'] for v in e['senses']));self.assertTrue(any('walk 的' in v['gloss'] for v in e['senses']))
 def test_invalid_and_nonchinese_data_not_imported(self):
  rows=[row('hello','n. 你好',phonetic='həˈləʊ'),row('blank','English only'),row('bad!','错误'),row('x'*64,'过长')]
  result,_=augment({},rows);self.assertEqual(set(result),{'hello'});self.assertEqual(json.loads(result['hello'])['pronunciation'],'həˈləʊ')
 def test_wiki_markup_sense_is_not_merged(self):
  old={'extraordinary':encode(dict(word='extraordinary',pronunciation='',source='维基词典',senses=[dict(pos='',gloss='====翻译==== 简体中文',synonyms=[])]))}
  result,audit=augment(old,[row('extraordinary','a. 非凡的')])
  self.assertNotIn('====',result['extraordinary'].decode());self.assertEqual(audit['counts']['removed_markup_or_empty_old_senses'],1)
 def test_binary_roundtrip_and_alias_dedup(self):
  v=entry('child','孩子'); data={'child':v,'children':v,'中':entry('中','中间')}
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'x.dict';write_dict(p,data);self.assertEqual(read_dict(p),data)

class RelationSafetyTest(unittest.TestCase):
 def test_lexical_senses_survive_inflection_mentions_and_old_aliases(self):
  rows=[row('glass','n. 玻璃','s:glasses'),row('glasses','n. 眼镜；双筒望远镜；玻璃（glass的复数形式）','0:glass/1:s'),row('airway','n. 空中航线','s:airways'),row('airways','n. 航空公司','0:airway/1:s'),row('acidulate','vt. 使酸化','p:acidulated'),row('acidulated','a. 带酸性的, 乖僻的, 易怒的','0:acidulate/1:p')]
  old={'airways':entry('airway','空中航线'),'acidulated':entry('acidulate','使酸化')}
  result,_=augment(old,rows)
  for w,meaning in [('glasses','眼镜'),('airways','航空公司'),('acidulated','易怒')]:
   e=json.loads(result[w]);self.assertEqual(e['word'],w);self.assertIn(meaning,e['senses'][0]['gloss'])
 def test_generated_label_narrows_to_explicit_source_form(self):
  rows=[row('rise','vi. 升起','p:rose'),row('rose','n. 玫瑰\\nrise的过去式','0:rise/1:pd')]
  result,_=augment({},rows);e=json.loads(result['rose'])
  self.assertEqual(e['senses'][0]['gloss'],'玫瑰');self.assertEqual(e['senses'][1]['gloss'],'rise 的过去式；升起')
 def test_literal_newline_before_lemma_is_decoded(self):
  rows=[row('find','vt. 发现','p:found'),row('found','vt. 建立\\nfind的过去式和过去分词','0:find/1:pd')]
  result,_=augment({},rows);e=json.loads(result['found'])
  self.assertEqual(e['word'],'found');self.assertEqual(e['senses'][0]['gloss'],'建立');self.assertTrue(any('find 的过去式/过去分词；发现' in v['gloss'] for v in e['senses']))
 def test_bad_exchange_does_not_override_explicit_lemma_or_pos(self):
  rows=[row('abl','[计] 结构框图语言','r:abler/t:ablest'),row('able','a. 能够的'),row('abler','a. 能够的(able的比较级)','0:abl/1:r'),row('ablest','a. 能够的(able的最高级)','0:abl/1:t'),row('zomby','僵停','s:zombies'),row('zombie','n. 僵尸'),row('zombies','n. 僵尸(zombie的复数形式)','0:zomby/1:s'),row('den','n. 兽穴','s:dens'),row('dens','den的复数','0:den/1:s/r:denser'),row('denser','a. 更密集的','0:dens/1:r')]
  result,_=augment({},rows)
  for w,bad in [('abler','结构框图'),('ablest','结构框图'),('zombies','僵停'),('denser','兽穴')]:
   self.assertEqual(json.loads(result[w])['word'],w);self.assertNotIn(bad,result[w].decode())

if __name__=='__main__':unittest.main()
