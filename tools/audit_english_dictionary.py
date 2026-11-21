"""独立人工常用词清单与词形挑战；非统计代表性频率榜。
Independent editorial everyday-word checklist and morphology challenge, not a statistical frequency claim.
"""
import csv,json,sys
from pathlib import Path
from build_english_dictionary import read_dict,normalize
COMMON='''the be to of and a in that have i it for not on with he as you do at this but his by from they we say her she or an will my one all would there their what so up out if about who get which go me when make can like time no just him know take people into year your good some could them see other than then now look only come its over think also back after use two how our work first well way even new want because any these give day most us is are was were been being has had did done went gone came took made said saw known man woman child father mother brother sister family friend home house room door window kitchen table chair bed food water bread rice milk tea coffee eat drink cook buy sell shop money price book read write learn school teacher student job office city country world street road car bus train plane travel walk run stop start move stay live die love hate help need feel find tell ask answer question problem reason result idea story history future past present right left turn light dark big small long short high low early late open close easy hard important possible different same happy sad beautiful cold hot warm rain snow wind sun moon sky sea river tree leaf flower dog cat bird fish animal body head face eye ear nose mouth hand arm leg foot heart mind life health sleep wake hear listen speak talk understand remember forget believe hope try keep leave bring hold put let begin end change grow build break meet lose win pay wait follow lead send receive sit stand watch play read vastness condense unconscious differently circulation jurisdiction fragment combine'''.split()
CHALLENGE='walked walking walks studies studied studying stopped stopping tries tried trying making babies wives leaves children teeth feet men women ran went gone taken wrote written does saw left lay news means business axes running better fragments combining vastness condense unconscious differently circulation jurisdiction'.split()
EXPECTED={'walked':'walk','babies':'baby','wives':'wife','children':'child','teeth':'tooth','feet':'foot','men':'man','women':'woman','ran':'run','went':'go','wrote':'write','does':'do','fragments':'fragment','combining':'combine'}
def main(folder,base):
 p=Path(folder);old=read_dict(base);new=read_dict(p/'staged/offline.dict')
 def coverage(words):
  words=sorted(set(words));return {'total':len(words),'before':sum(w in old for w in words),'after':sum(w in new for w in words),'missing_before':[w for w in words if w not in old],'missing_after':[w for w in words if w not in new]}
 samples={}
 for w in CHALLENGE:
  e=json.loads(new[w]) if w in new else None
  samples[w]={'canonical':e['word'] if e else None,'senses':e['senses'][:2] if e else []}
 failures={w:(dest,samples[w]['canonical']) for w,dest in EXPECTED.items() if samples[w]['canonical']!=dest}
 freq=[]
 for r in csv.DictReader((p/'ecdict.csv').open()):
  if r['bnc'].isdigit() and 0<int(r['bnc'])<=1000:freq.append(normalize(r['word']))
 user_examples={w:{'dictionary_key':k,'record':json.loads(new[k]),'possessive_runtime_test_required':w!=k} for w,k in [(w,w) for w in 'vastness condense unconscious fragments combining differently circulation'.split()]+[("jurisdiction's",'jurisdiction')]}
 review_cases={w:json.loads(new[w]) for w in 'glasses airways acidulated abler ablest zombies denser found rose written'.split()}
 for w,meaning in [('glasses','眼镜'),('airways','航空公司'),('acidulated','易怒')]:
  assert review_cases[w]['word']==w and meaning in review_cases[w]['senses'][0]['gloss'],w
 for w,bad in [('abler','结构框图'),('ablest','结构框图'),('zombies','僵停'),('denser','兽穴')]:
  assert review_cases[w]['word']==w and bad not in json.dumps(review_cases[w],ensure_ascii=False),w
 for w,lemma in [('found','find'),('rose','rise'),('written','write')]:
  assert any(v['gloss'].startswith(lemma+' 的') for v in review_cases[w]['senses']),w
 assert any(v['gloss'].startswith('rise 的过去式；') for v in review_cases['rose']['senses'])
 report={'independent_review_regressions':review_cases,'actual_user_examples':user_examples,'independent_editorial_checklist':coverage(COMMON),'inflection_and_user_challenge':coverage(CHALLENGE),'source_internal_bnc_rank_1_1000':coverage(freq),'canonical_failures':failures,'samples':samples,'scope':'Editorial checklist independent of source selection, not a statistically representative or exhaustive English coverage measure. BNC ranks are source-internal only.'}
 (p/'staged/english-coverage.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(json.dumps({k:({a:b for a,b in v.items() if not a.startswith('missing')} if isinstance(v,dict) else v) for k,v in report.items() if k not in ('samples','actual_user_examples','independent_review_regressions')},ensure_ascii=False,indent=2))
 assert not failures,failures
 assert all(new[k]==b for k,b in old.items() if not k.isascii())
 assert any('去' in s['gloss'] for s in json.loads(new['went'])['senses'])
 assert all('\ufe01' not in s['gloss'] for s in json.loads(new['circulation'])['senses'])
if __name__=='__main__':main(*sys.argv[1:])
