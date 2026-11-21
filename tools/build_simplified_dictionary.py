#!/usr/bin/env python3
"""Build simplified SORDICT1 from licensed Wiktionary + KFCD sources.

Requires opencc-python-reimplemented==0.1.7 and official OpenCC 1.1.9
TSPhrases.txt / TSCharacters.txt in --conversion-data. Never reads MOE.
"""
import argparse
from collections import Counter, defaultdict
from functools import lru_cache
import gzip
import hashlib
import importlib.metadata
import json
from pathlib import Path
import re
import struct
import unicodedata
import zipfile
from opencc import OpenCC

HEADER=struct.Struct('<8s6I'); ROW=struct.Struct('<64sQII')
HAN=re.compile(r'[㐀-䶿一-鿿豈-﫿\U00020000-\U000323af]')
EN=re.compile(r"[A-Za-z]+(?:[ ’'\-][A-Za-z]+)*")
LOWER=str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ’‘', "abcdefghijklmnopqrstuvwxyz''")

def normalize(s): return ' '.join(unicodedata.normalize('NFKC',s).translate(LOWER).split())
def valid(s,en=False):
    return bool(s) and len(s.encode())<64 and bool(EN.fullmatch(s) if en else all(HAN.fullmatch(c) for c in s))
def digest(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()
def load_converter(folder):
    # Feed versioned official data to the Apache-2.0 Python OpenCC algorithm.
    c=OpenCC()
    c._dict_chain_data=[]
    c._add_dictionaries([[str(Path(folder).resolve()/n) for n in ('TSPhrases.txt','TSCharacters.txt')]], c._dict_chain_data)
    c._dict_init_done=True
    @lru_cache(maxsize=300000)
    def stable(text):
        # Phrase conversion can expose a second match (等同於單數 -> 等同於单数).
        for _ in range(8):
            converted=c.convert(text)
            if converted==text: return text
            text=converted
        raise ValueError('OpenCC conversion failed to stabilize')
    return stable
def tone_pinyin(s):
    marks={'a':'āáǎà','e':'ēéěè','i':'īíǐì','o':'ōóǒò','u':'ūúǔù','ü':'ǖǘǚǜ'}
    def sub(m):
        syll=m[1].replace('u:','ü').replace('v','ü'); tone=int(m[2])
        if tone in (0,5): return syll
        at=syll.find('a')
        if at<0: at=syll.find('e')
        if at<0 and 'ou' in syll: at=syll.find('o')
        if at<0:
            at=next((i for i in range(len(syll)-1,-1,-1) if syll[i] in marks),-1)
        if at<0: return unicodedata.normalize('NFC',syll+['\u0304','\u0301','\u030c','\u0300'][tone-1])
        return syll[:at]+marks[syll[at]][tone-1]+syll[at+1:]
    return re.sub(r'([a-zü:]+)([0-5])',sub,s.lower())
def wiki_prons(r,en):
    result=[]
    for sound in r.get('sounds',[]):
        tags=set(sound.get('tags',[]))
        if en: pron=sound.get('ipa','')
        elif {'Pinyin','Mandarin'}<=tags and not tags.intersection({'Erhua','Sichuanese','Chengdu','Dongan'}):
            pron=re.sub(r'\s*\([^)]*\)$','',sound.get('zh_pron',''))
        else: continue
        if pron and len(pron.encode())<192 and pron not in result:
            result.append(pron)
            if en: break
    return result
def build(raw,output,kfcd,conversion_data,unihan=None,supplement=None,hanzidict=None):
    convert=load_converter(conversion_data); entries={}; aliases=defaultdict(list); counts=Counter()
    readings={}; frequencies={}; definitions={}; traditional={}
    if unihan:
        with zipfile.ZipFile(unihan) as z:
            for n in ('Unihan_Readings.txt','Unihan_Variants.txt'):
                for line in z.read(n).decode().splitlines():
                    if not line.startswith('U+'): continue
                    code,field,value=line.split('\t'); ch=chr(int(code[2:],16))
                    if field=='kMandarin': readings[ch]=value.split()[0]
                    elif field=='kHanyuPinlu': frequencies[ch]=sum(map(int,re.findall(r'\((\d+)\)',value)))
                    elif field=='kDefinition': definitions[ch]=value
                    elif field=='kTraditionalVariant' and ' ' not in value: traditional[ord(ch)]=chr(int(value[2:],16))
    def alias(src,dest):
        if src!=dest and dest not in aliases[src]: aliases[src].append(dest)
    def canon(w): return normalize(convert(w))
    with gzip.open(raw,'rt',encoding='utf-8') as f:
        for line in f:
            r=json.loads(line)
            if r.get('pos')=='hard-redirect':
                src=normalize(r.get('title','')); dest=canon(r.get('redirect','').split('#')[0])
                if (valid(src) and valid(dest)) or (valid(src,True) and valid(dest,True)): alias(src,dest)
                continue
            lang=r.get('lang_code')
            if lang not in ('zh','cmn','en'): continue
            en=lang=='en'; original=normalize(r['word']); key=canon(r['word'])
            if not valid(key,en): continue
            counts['input_'+lang]+=1
            alias(original,key)
            if not en:
                for form in r.get('forms',[]):
                    if {'Simplified-Chinese','Traditional-Chinese'} & set(form.get('tags',[])):
                        v=normalize(form['form'])
                        if valid(v): alias(v,key)
            prons=wiki_prons(r,en); pron=prons[0] if len(prons)==1 else ''; pos=convert(r.get('pos_title',''))
            if len(pos.encode())>=24: pos=''
            senses=[]
            for s in r.get('senses',[]):
                refs=s.get('form_of',[])+s.get('alt_of',[])
                if refs:
                    for ref in refs:
                        dest=canon(ref['word'])
                        if valid(dest,en): alias(key,dest)
                    continue
                glosses=s.get('glosses',[])
                gloss=convert('；'.join(dict.fromkeys(' '.join(g.split()) for g in glosses if g.strip())))
                if '__NOTITLECONVERT__' in gloss or '发音和释义，请见' in gloss:
                    target=re.search(r'请见「([^」（]+)',gloss)
                    if target and valid(canon(target[1])): alias(key,canon(target[1]))
                    continue
                if not HAN.search(gloss) or gloss==key or len(gloss.encode())>=768: continue
                synonyms=[]
                if en:
                    for link in s.get('synonyms',[])+[v for v in r.get('synonyms',[]) if v.get('sense') in glosses]:
                        name=convert(link.get('word',''))
                        if EN.fullmatch(name) and normalize(name)!=key and name not in synonyms and len(', '.join(synonyms+[name]).encode())<192: synonyms.append(name)
                sense={'pos':pos,'gloss':gloss,'synonyms':synonyms}
                if pron: sense['pronunciation']=pron
                senses.append(sense)
            if not senses: continue
            e=entries.setdefault(key,{'word':convert(r['word']),'pronunciation':'','source':'维基词典','senses':[]})
            known=e.setdefault('_pronunciations',[])
            for value in prons:
                if value not in known: known.append(value)
            for s in senses:
                if len(e['senses'])<12 and s not in e['senses']: e['senses'].append(s)
    wiki_keys=set(entries)
    kentries={}; krows=0; kdefined=0
    for line in Path(kfcd).read_text().splitlines():
        cells=line.split('\t')
        if len(cells)!=6: raise ValueError('KFCD requires six columns')
        trad,simp,pinyin,examples,definition,variants=cells; krows+=1
        if not definition.strip(): continue # Examples are never passed off as definitions.
        kdefined+=1; key=canon(simp)
        if not valid(key): continue
        gloss=convert(definition.strip())
        if not HAN.search(gloss) or len(gloss.encode())>=768: continue
        e=kentries.setdefault(key,{'word':convert(simp),'pronunciation':'','source':'开放汉语字典','senses':[]})
        sense={'pos':'','gloss':gloss,'synonyms':[],'pronunciation':tone_pinyin(pinyin)}
        if len(e['senses'])<12 and sense not in e['senses']: e['senses'].append(sense)
        alias(trad,key)
        for variant in re.split('[，、, ]',variants):
            if valid(variant): alias(variant,key)
    # KFCD takes precedence as a whole entry: avoids attributing its pinyin to Wiki senses.
    entries.update(kentries)
    supplemental=[]
    if supplement:
        supplemental=json.loads(Path(supplement).read_text())
        for r in supplemental:
            key=canon(r['word']); e={k:r[k] for k in ('word','pronunciation','source','senses')}
            e['word']=convert(e['word'])
            for s in e['senses']:
                for field in ('gloss','pos'): s[field]=convert(s.get(field,''))
                s['synonyms']=[convert(w) for w in s.get('synonyms',[])]
            entries[key]=e
            for a in r.get('aliases',[]): alias(a,key)
    hd_count=0; hd_senses=0; hd_source_counts=Counter(); hd_snapshot=None; hd_bad_readings=[]
    if hanzidict:
        hd=json.loads(Path(hanzidict).read_text()); hd_snapshot=hd.get('meta',{}).get('snapshot')
        for row in hd['characters']:
            # 规范字原键不可整体繁转简：乾(qián)和干(gān)都是规范字。
            # Keep standard-character identity: 乾 and 干 are separate standard entries.
            key=normalize(row['char'])
            if len(key)!=1 or not valid(key): raise ValueError('invalid HanziDict character')
            groups=[]
            for reading in row.get('readings',[])+([{'pinyin':readings.get(key,''),'senses':row['senses']}] if row.get('senses') else []):
                senses=[]; pron=reading.get('pinyin','')
                if len(pron.encode())>=192: raise ValueError('oversize HanziDict pronunciation')
                if pron and (not any('LATIN' in unicodedata.name(c,'') for c in pron) or
                    not all(c in " ·'-’" or 'LATIN' in unicodedata.name(c,'') or unicodedata.category(c).startswith('M') for c in pron)):
                    hd_bad_readings.append({'word':key,'source_pinyin':pron})
                    continue  # 原始汉字引用不是拼音，不将此组词义误配给其他读音。/ A Han reference is not pinyin; never reassign this group to another reading.

                for sense in reading.get('senses',[]):
                    hd_source_counts[sense.get('source','unknown')]+=1
                    if sense.get('source')!='HanziDict': continue
                    gloss=convert(' '.join(sense.get('content','').split()))
                    if not gloss: continue
                    if not HAN.search(gloss) or len(gloss.encode())>=768: raise ValueError('invalid HanziDict gloss')
                    pos=convert(sense.get('pos',''))
                    item={'pos':pos if len(pos.encode())<24 else '', 'gloss':gloss, 'synonyms':[]}
                    if pron: item['pronunciation']=pron
                    if item not in senses: senses.append(item)
                if senses: groups.append(senses)
            # 先保留每个读音的首义，再补其他义；不将一音的读法套到另一义。
            # Round-robin readings so every reading gets its first sense before extra senses.
            senses=[]
            for i in range(max(map(len,groups),default=0)):
                for group in groups:
                    if i<len(group) and group[i] not in senses and len(senses)<12: senses.append(group[i])
            if not senses: continue
            entries[key]={'word':key,'pronunciation':'','source':'HanziDict','senses':senses}
            hd_count+=1; hd_senses+=len(senses)
            for variant in row.get('aliases',[]):
                if valid(variant): alias(normalize(variant),key)
    for key,e in entries.items():
        ps=e.pop('_pronunciations',[]) or list(dict.fromkeys(s['pronunciation'] for s in e['senses'] if s.get('pronunciation')))
        # Unmapped alternatives stay at entry level; never invent a sense/reading pair.
        if not e['pronunciation']:
            joined=' · '.join(ps)
            e['pronunciation']=joined if ps and len(joined.encode())<192 else (readings.get(key,'') if not ps else '')
        if valid(key):
            v=key.translate(traditional)
            if v!=key: alias(v,key)
    def resolve(key,seen):
        if key in entries: return key
        if key in seen or len(seen)>12: return None
        for dest in aliases.get(key,[]):
            found=resolve(dest,seen|{key})
            if found: return found
    keys={k:k for k in entries}
    for key in sorted(aliases):
        found=resolve(key,set())
        if found and (valid(key) or valid(key,True)): keys[key]=found
    output=Path(output); output.parent.mkdir(parents=True,exist_ok=True)
    offset=HEADER.size+ROW.size*len(keys); index={}
    with output.open('w+b') as f:
        f.seek(offset)
        for key,e in sorted(entries.items()):
            blob=json.dumps(e,ensure_ascii=False,separators=(',',':')).encode()
            if len(blob)>16384: raise ValueError('oversize '+key)
            index[key]=(f.tell(),len(blob)); f.write(blob)
        size=f.tell(); f.seek(0); f.write(HEADER.pack(b'SORDICT1',1,len(keys),HEADER.size,offset,size,0))
        for key,dest in sorted(keys.items()): f.write(ROW.pack(key.encode(),*index[dest],0))
    counts.update(keys=len(keys),entries=len(entries),alias_keys=len(keys)-len(entries),english_keys=sum(bool(EN.fullmatch(k)) for k in keys),chinese_single_keys=sum(len(k)==1 and bool(HAN.fullmatch(k)) for k in keys),simplified_chinese_single_entries=sum(len(k)==1 and bool(HAN.fullmatch(k)) for k in entries),kfcd_rows=krows,kfcd_defined_rows=kdefined,kfcd_defined_entries=len(kentries),kfcd_added_entries=len(set(kentries)-wiki_keys),supplemental_entries=len(supplemental),reverse_translation_entries=0,entries_with_synonyms=sum(any(s['synonyms'] for s in e['senses']) for e in entries.values()))
    missing=[]; missing_seen=set()
    for ch,frequency in sorted(frequencies.items(),key=lambda x:(-x[1],x[0])):
        key=canon(ch)
        if key in keys or key in missing_seen: continue
        missing_seen.add(key)
        missing.append({'word':key,'source_character':ch,'frequency':frequency,'unihan_english_definition':definitions.get(ch,''),'pinyin':readings.get(ch,'')})
    output.with_name('missing-common-characters.json').write_text(json.dumps(missing[:50],ensure_ascii=False,indent=2)+'\n')
    manifest={'format':'SORDICT1','version':'simplified-1','builder_sha256':digest(Path(__file__)),'source_entry_counts':dict(Counter(e['source'] for e in entries.values())),'file_sha256':digest(output),'bytes':size,'counts':dict(counts),'wiktionary':{'url':'https://kaikki.org/zhwiktionary/raw-wiktextract-data.jsonl.gz','sha256':digest(raw),'license':'CC-BY-SA-4.0'},'kfcd':{'url':'https://github.com/kfcd/hyzd','revision':'04987755b16264636c01d28df4d96fa14dadd210','sha256':digest(kfcd),'license':'CC-BY-3.0'},'conversion':{'code':'opencc-python-reimplemented','code_version':importlib.metadata.version('opencc-python-reimplemented'),'code_license':'Apache-2.0','data':'BYVoid/OpenCC','data_version':'ver.1.1.9','data_license':'Apache-2.0','data_sha256':{n:digest(Path(conversion_data)/n) for n in ('TSPhrases.txt','TSCharacters.txt')}},'unihan':{'version':'17.0.0','sha256':digest(unihan),'license':'Unicode-3.0'} if unihan else None,'supplement_sha256':digest(supplement) if supplement else None,'changes':['简体转换所有字头、词性、释义与同义词；保留繁体查询别名','每条义项保留来源对应拼音；KFCD字头整体优先','只收录真实释义；不将词例或只有译词的条目当完整定义','不含MOE，不含部首笔画注音全文；补充译释明示来源']}
    if hanzidict:
        manifest['version']='simplified-2'
        manifest['hanzidict']={'url':'https://github.com/sinobaba/hanzidict','revision':'9bbe0b0cce38e8efa4dca74ea263268b9e6af895','sha256':digest(hanzidict),'snapshot':hd_snapshot,'license':'MIT','included_source':'HanziDict','entries':hd_count,'senses':hd_senses,'input_sense_sources':dict(hd_source_counts)}
        manifest['hanzidict']['input_selection']=hd.get('meta',{}).get('selection')
        manifest['hanzidict']['excluded_invalid_reading_groups']=hd_bad_readings
        manifest['changes']+=['新增 HanziDict 原创中文义项，排除所有非 HanziDict 来源行；字头整体优先','保留规范字原键、多音字逐义拼音；按读音轮流取义至多12条，保留已有词语和英文库']
    output.with_name('manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    return manifest
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('source',type=Path); p.add_argument('output',type=Path); p.add_argument('--kfcd',type=Path,required=True); p.add_argument('--conversion-data',type=Path,required=True); p.add_argument('--unihan',type=Path); p.add_argument('--supplement',type=Path); p.add_argument('--hanzidict',type=Path)
    a=p.parse_args(); print(json.dumps(build(a.source,a.output,a.kfcd,a.conversion_data,a.unihan,a.supplement,a.hanzidict),ensure_ascii=False,indent=2))
