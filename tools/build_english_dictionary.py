#!/usr/bin/env python3
"""以 ECDICT 中文短释义扩充 SORDICT1；保留全部中文载荷。
Augment SORDICT1 with ECDICT Chinese senses; preserve every Chinese payload.
"""
import argparse,csv,hashlib,json,re,struct,unicodedata
from collections import Counter,defaultdict
from pathlib import Path
HEADER=struct.Struct('<8s6I');ROW=struct.Struct('<64sQII')
REVISION='39cfb9672c0906b63569e0453c2e6abfb9ab182a'
SOURCE_SHA={'ecdict.csv':'d1ecbdf9ee2b920afb6f430ca6b6bb9f559588cf0dff86e04bd255f9390843cc','README.md':'0c3f95fc7e15f7eccf65feedb58ba3ebace1fe7b7cbdd1af60c9ace4cef9094b','LICENSE':'f8552dd246f61a4e064569eae6194a01c6b3d63b03bf27c6ca863593c549ed0f'}
BASE_SHA='1f0de1b1875b06b4d79e5da81b6f36db93d1cbff27e5c6f0d31d526cbad9a3ab'
EN=re.compile(r"[a-z]+(?:[ '\-][a-z]+)*")
HAN=re.compile(r'[㐀-䶿一-鿿豈-﫿\U00020000-\U000323af]')
FORM_TEXT=r'(?:过去(?:式|分词)|现在分词|第三人称单数|复数|比较级|最高级|ing形式)'
LEMMA_REF=re.compile(r"([a-z][a-z'\-]*)\s*的\s*(?:名词|动词|形容词)?\s*"+FORM_TEXT+r'(?:\s*(?:和|及|与|、|/|或)\s*'+FORM_TEXT+r')*',re.I)
PURE_FORM=re.compile(r"[a-z][a-z'\-]*\s*的\s*(?:名词|动词|形容词)?\s*"+FORM_TEXT+r'(?:\s*(?:和|及|与|、|/|或)\s*'+FORM_TEXT+r')*(?:形式)?[。.]?',re.I)
# 逐项人工复核的不规则变形，仍要求原词正向 exchange 佐证。
# Reviewed irregular forms still require a corroborating forward exchange.
REVIEWED={'walked':'walk','fragments':'fragment','babies':'baby','combining':'combine','does':'do','wives':'wife','children':'child','teeth':'tooth','feet':'foot','men':'man','women':'woman'}
PRESERVE=set('saw left lay news means business axes running better trying'.split())
def normalize(s):return ' '.join(unicodedata.normalize('NFKC',s).lower().replace('’',"'").replace('‘',"'").split())
def valid(s):return len(s.encode())<64 and bool(EN.fullmatch(s))
def clean(s):return ' '.join(''.join(c for c in s if unicodedata.category(c)!='Cf' and not 0xfe00<=ord(c)<=0xfe0f).split())
def useful_gloss(s):
 return bool(HAN.search(s)) and not re.search(r'={2,}.*?={2,}|__NOTITLECONVERT__|\{\{|\}\}',s) and s.strip() not in {'简体中文','繁体中文','翻译','中文'}
def encode(e):return json.dumps(e,ensure_ascii=False,separators=(',',':')).encode()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read_dict(path):
 b=Path(path).read_bytes();magic,version,count,start,data,size,res=HEADER.unpack_from(b)
 if magic!=b'SORDICT1' or version!=1 or start!=32 or data!=32+80*count or size!=len(b):raise ValueError('invalid dictionary header')
 result={};previous=b''
 for i in range(count):
  k,p,n,_=ROW.unpack_from(b,start+80*i); k=k.rstrip(b'\0')
  if k<=previous or p<data or p+n>size or n>16384:raise ValueError('invalid dictionary index')
  previous=k;result[k.decode()]=b[p:p+n]
 return result
def write_dict(path,keys):
 path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);offset=32+80*len(keys);spans={}
 with path.open('wb') as f:
  f.seek(offset)
  for k,b in sorted(keys.items()):
   if len(b)>16384:raise ValueError('oversize record')
   if b not in spans:spans[b]=(f.tell(),len(b));f.write(b)
  size=f.tell();f.seek(0);f.write(HEADER.pack(b'SORDICT1',1,len(keys),32,offset,size,0))
  for k,b in sorted(keys.items()):f.write(ROW.pack(k.encode(),*spans[b],0))
def exchange(row):
 result=defaultdict(set)
 for item in row.get('exchange','').split('/'):
  if ':' not in item:continue
  typ,values=item.split(':',1)
  for v in values.split(','):
   v=normalize(v)
   if valid(v):result[typ].add(v)
 return result
def form_only(glosses):
 lines=[s for g in glosses for s in g.replace('\\n','\n').splitlines() if s.strip()]
 return bool(lines) and all(PURE_FORM.fullmatch(re.sub(r'^(?:n|v|vt|vi|a|adj|adv|pl)\.\s*','',clean(g))) for g in lines)
def source_entry(row):
 senses=[]
 for line in row.get('translation','').replace('\\n','\n').splitlines():
  line=clean(line)
  if not useful_gloss(line):continue
  m=re.match(r'^(n|v|vt|vi|a|adj|adv|prep|pron|conj|interj|pl|num|aux)\.\s*',line)
  pos=m[1]+'.' if m else '';gloss=line[m.end():] if m else line
  # 超长行在标点边界缩短；不截断 UTF-8。/ Shorten long lines at punctuation, never within UTF-8.
  if len(gloss.encode())>=768:
   parts=re.split(r'(?<=[,，;；。])',gloss);gloss=''
   for part in parts:
    if len((gloss+part+'…').encode())>=768:break
    gloss+=part
   if not gloss:continue
   gloss+='…'
  s={'pos':pos,'gloss':gloss,'synonyms':[]}
  if s not in senses:senses.append(s)
  if len(senses)==12:break
 if not senses:return None
 pron=clean(row.get('phonetic',''));pron=pron if len(pron.encode())<192 else ''
 return {'word':normalize(row['word']),'pronunciation':pron,'source':'ECDICT','senses':senses}
def augment(old,rows):
 stats=Counter();sources={};forward=defaultdict(set);reverse=defaultdict(set);relation_types=defaultdict(set)
 for r in rows:
  stats['input_rows']+=1;k=normalize(r['word'])
  if not valid(k):stats['rejected_key']+=1;continue
  if k in sources:stats['duplicate_normalized_key']+=1;continue
  e=source_entry(r)
  if not e:stats['rejected_no_chinese_sense']+=1;continue
  sources[k]=(e,r);stats['accepted_source_rows']+=1
  for typ,forms in exchange(r).items():
   if typ in 'pdi3rts' and len(typ)==1:
    for f in forms:
     if f!=k:forward[f].add(k);relation_types[f,k].add(typ)
   if typ=='0':
    reverse[k].update(forms-{k})
    mt=re.search(r'(?:^|/)1:([pdi3rts]+)(?:/|$)',r.get('exchange',''))
    if mt:
     for dest in forms-{k}:relation_types[k,dest].update(mt[1])
 old_entries={}
 for k,b in old.items():
  if valid(k):
   e=json.loads(b);canonical=normalize(e['word'])
   if valid(canonical):old_entries[canonical]=e
 entries=dict(old_entries);aliases={}
 for k,(e,r) in sources.items():
  previous=old_entries.get(k)
  if previous:
   old_senses=[]
   for s in previous['senses']:
    s=dict(s);s['gloss']=clean(s['gloss'])
    if useful_gloss(s['gloss']):old_senses.append(s)
    else:stats['removed_markup_or_empty_old_senses']+=1
   unique={s['gloss'] for s in e['senses']}
   for s in old_senses:
    if s['gloss'] not in unique and len(e['senses'])<12:e['senses'].append(s);unique.add(s['gloss'])
   e['source']='ECDICT · '+previous['source'];e['pronunciation']=e['pronunciation'] or previous.get('pronunciation','')
   stats['enriched_existing_entries']+=1
  entries[k]=e
 # 两种用途共用关系校验，避免同一错误词根进入别名或首条释义。
 # Validate relations once for both aliases and lemma senses.
 original_senses={k:list(e['senses']) for k,e in entries.items()}
 def trusted_types(k,dest):
  types=relation_types[k,dest]
  text=sources[k][1]['translation'].replace('\\n','\n') if k in sources else ''
  references=[m[0] for m in LEMMA_REF.finditer(text) if normalize(m[1])==dest]
  if references:
   stated={typ for phrase,typ in [('过去式','p'),('过去分词','d'),('现在分词','i'),('ing形式','i'),('第三人称单数','3'),('复数','s'),('比较级','r'),('最高级','t')] if any(phrase in ref for ref in references)}
   types=types&stated
  return types
 def relation_ok(k,dest):
  if dest not in entries or k==dest:return False
  text=sources[k][1]['translation'].replace('\\n','\n') if k in sources else ''
  named={normalize(m[1]) for m in LEMMA_REF.finditer(text)}
  if named and named!={dest}:return False
  types=trusted_types(k,dest)
  if not types:return False
  poses={v.get('pos','') for v in original_senses[dest]}
  allowed=set()
  if types&set('pdi3'):allowed.update(('v.','vt.','vi.','动词'))
  if 's' in types:allowed.update(('n.','名词'))
  if types&set('rt'):allowed.update(('a.','adj.','adv.','形容词','副词'))
  return bool(poses&allowed)
 for k in sorted(set(forward)|set(reverse)):
  candidates=forward[k]|reverse[k]
  reviewed=REVIEWED.get(k)
  if reviewed and reviewed in forward[k]:candidates={reviewed}
  if len(candidates)!=1:stats['ambiguous_forms_preserved']+=1;continue
  dest=next(iter(candidates))
  if not relation_ok(k,dest) or k in PRESERVE:continue
  previous=old_entries.get(k)
  if previous and not form_only([s['gloss'] for s in previous['senses']]):continue
  if k in sources and not reviewed and not form_only([sources[k][1]['translation']]):continue
  aliases[k]=dest
 def resolve(k,seen=()):
  if k in seen:return k
  return resolve(aliases[k],seen+(k,)) if k in aliases else k
 # 独立词义保留时补充词根首义，明确标识词形；不猜同形异义词。
 # Retained lexical forms receive an explicitly labeled lemma sense, without guessing homographs.
 for k,(e,r) in sources.items():
  if k in aliases or k in PRESERVE-{'running'}:continue
  candidates=forward[k]|reverse[k]
  if len(candidates)!=1:continue
  dest=resolve(next(iter(candidates)))
  if not relation_ok(k,dest):continue
  # 类型字段不一定是英文词键，直接从原行读取。/ Type codes are not word keys; read the raw field.
  mt=re.search(r'(?:^|/)1:([pdi3rts]+)(?:/|$)',r.get('exchange',''))
  if not mt:continue
  forms=''.join(c for c in mt[1] if c in trusted_types(k,dest))
  if not forms:continue
  label='/'.join(dict.fromkeys({'p':'过去式','d':'过去分词','i':'现在分词','3':'第三人称单数','s':'复数','r':'比较级','t':'最高级'}[c] for c in forms))
  preferred=('v.','vt.','vi.','动词') if any(c in forms for c in 'pdi3') else (('n.','名词') if 's' in forms else ('a.','adj.','adv.','形容词','副词'))
  base_sense=next((v for v in original_senses[dest] if v.get('pos') in preferred),None)
  if base_sense is None:continue
  sense=dict(base_sense);sense['gloss']=dest+' 的'+label+'；'+sense['gloss']
  if len(sense['gloss'].encode())>=768:continue
  sense.pop('pronunciation',None)
  e['senses']=(e['senses'][:1]+[sense]+e['senses'][1:])[:12];stats['lexical_forms_with_lemma_sense']+=1
 blobs={k:encode(e) for k,e in entries.items()};result=dict(old)
 for k,b in old.items():
  if valid(k):
   canonical=normalize(json.loads(b)['word']);dest=resolve(canonical)
   if dest in blobs:result[k]=blobs[dest]
 for k in entries:result[k]=blobs[resolve(k)]
 for k in aliases:result[k]=blobs[resolve(k)]
 # 仅有一个词根的缺失形式可直接复用词根载荷。/ Missing unambiguous forms reuse the lemma payload.
 for k,targets in forward.items():
  if k not in result and len(targets)==1:
   dest=resolve(next(iter(targets)))
   if dest in blobs and relation_ok(k,dest):result[k]=blobs[dest];aliases[k]=dest
 stats.update(keys_before=len(old),keys_after=len(result),added_keys=len(set(result)-set(old)),explicit_aliases=len(aliases),english_keys=sum(valid(k) for k in result),records=len(set(result.values())))
 assert all(result[k]==v for k,v in old.items() if not valid(k))
 return result,{'counts':dict(stats),'aliases':{k:resolve(k) for k in sorted(aliases)}}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if sha(a.base)!=BASE_SHA:raise ValueError('base dictionary hash differs from accepted version')
 src=a.source_dir;commit=json.loads((src/'commit.json').read_text())
 if commit['sha']!=REVISION:raise ValueError('wrong source revision')
 for name,expected in SOURCE_SHA.items():
  if sha(src/name)!=expected:raise ValueError('source hash mismatch: '+name)
 old=read_dict(a.base)
 with (src/'ecdict.csv').open(newline='',encoding='utf-8') as f:result,audit=augment(old,csv.DictReader(f))
 write_dict(a.output,result)
 audit.update(format='SORDICT1',version='english-1',base_sha256=sha(a.base),file_sha256=sha(a.output),bytes=a.output.stat().st_size,builder_sha256=sha(__file__),source={'repository':'https://github.com/skywind3000/ECDICT','revision':REVISION,'license':'MIT','notice':'Copyright (c) 2025 Linwei','sha256':{n:sha(src/n) for n in ['ecdict.csv','README.md','LICENSE']},'provenance_note':'Upstream README describes mixed historical sources including EDictAZ, crawled materials and cdict. Repository declares MIT; underlying rights have not been independently verified. lemma.en.txt is excluded from build and redistribution due its research/education-only header.'},preserved_non_english_keys=sum(not valid(k) for k in old),chinese_payloads_byte_identical=True)
 a.output.with_name('english-manifest.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n')
 print(json.dumps({k:v for k,v in audit.items() if k!='aliases'},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
