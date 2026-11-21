/* SPDX-License-Identifier: Apache-2.0
 * 中文：按 UTF-8 原文字节识别句界，再裁到当前页；规则不推断语义。
 * English: Recognize sentence boundaries in original UTF-8 bytes, then clip to the page; rules do not infer meaning.
 * 冻结：有限线性扫描，不分配、不读文件、不查字体；缩写表是明确有限的启发规则。
 * Frozen: Bounded linear scans, no allocation, files or font lookup; abbreviations use explicit limited heuristics.
 */
#include "reader_sentence.h"
#include <stdint.h>
#include <string.h>

static size_t decode(const char *s, size_t left, uint32_t *cp) {
    if (!left) return 0;
    const unsigned char *p=(const unsigned char *)s;
    uint32_t c=p[0];
    if (c<0x80) { *cp=c; return 1; }
    size_t n;
    uint32_t min;
    if (c>=0xc2 && c<=0xdf) { n=2; min=0x80; c&=0x1f; }
    else if (c>=0xe0 && c<=0xef) { n=3; min=0x800; c&=0x0f; }
    else if (c>=0xf0 && c<=0xf4) { n=4; min=0x10000; c&=7; }
    else return 0;
    if (n>left) return 0;
    for (size_t i=1;i<n;++i) {
        if ((p[i]&0xc0)!=0x80) return 0;
        c=(c<<6)|(p[i]&0x3f);
    }
    if (c<min || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0;
    *cp=c; return n;
}
static bool alpha(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z');
}
static bool upper(unsigned char c) { return c>='A' && c<='Z'; }
static bool digit(unsigned char c) { return c>='0' && c<='9'; }
static unsigned char lower(unsigned char c) { return upper(c) ? (unsigned char)(c+'a'-'A') : c; }
static bool white(uint32_t c) {
    return c==' ' || (c>=9 && c<=13) || c==0x85 || c==0xa0 || c==0x1680 ||
        (c>=0x2000 && c<=0x200a) || c==0x2028 || c==0x2029 || c==0x202f || c==0x205f || c==0x3000;
}
static bool newline(uint32_t c) { return c=='\n' || c=='\r' || c==0x2028 || c==0x2029; }
static bool terminal(uint32_t c) {
    return c=='.' || c=='!' || c=='?' || c==0x3002 || c==0xff01 || c==0xff1f || c==0x2026;
}
static bool closer(uint32_t c) {
    return c=='\'' || c=='"' || c==')' || c==']' || c=='}' ||
        c==0x2019 || c==0x201d || c==0xff09 || c==0x3011 || c==0x3009 ||
        c==0x300b || c==0x300d || c==0x300f || c==0x3015;
}
static bool protected_period(const char *s,size_t len,size_t i) {
    unsigned char prev=i ? (unsigned char)s[i-1] : 0;
    unsigned char next=i+1<len ? (unsigned char)s[i+1] : 0;
    if (prev=='.' || next=='.') return false;
    if (digit(prev) && digit(next)) return true;
    // 连续字母点缩写只看邻近字节；不反复回扫长词。/ Dotted letter abbreviations inspect nearby bytes, never rescan long words.
    if (alpha(prev) && alpha(next) && i+2<len && s[i+2]=='.') return true;
    if (alpha(prev) && i>=3 && s[i-2]=='.' && alpha((unsigned char)s[i-3])) return true;
    static const char *const words[]={"mr","dr","mrs","ms","prof","st","vs","etc"};
    for (size_t w=0;w<sizeof(words)/sizeof(words[0]);++w) {
        size_t n=strlen(words[w]);
        if (i<n || (i>n && (alpha((unsigned char)s[i-n-1]) || s[i-n-1]=='.'))) continue;
        size_t j=0;
        while (j<n && lower((unsigned char)s[i-n+j])==(unsigned char)words[w][j]) ++j;
        if (j==n) return true;
    }
    // 单个大写首字母后接大写姓名；不跨段落寻找。/ A single uppercase initial followed by an uppercase name; never search across a paragraph.
    if (upper(prev) && (i==1 || (!alpha((unsigned char)s[i-2]) && s[i-2]!='.'))) {
        size_t j=i+1;
        while (j<len && (s[j]==' ' || s[j]=='\t')) ++j;
        if (j<len && upper((unsigned char)s[j])) return true;
    }
    return false;
}
static bool clipped(const char *text,size_t begin,size_t finish,size_t ps,size_t pe,
                    size_t at,size_t *start,size_t *end) {
    size_t a=begin>ps ? begin : ps, b=finish<pe ? finish : pe;
    size_t first=b,last=a;
    for (size_t i=a;i<b;) {
        uint32_t c;
        size_t n=decode(text+i,b-i,&c);
        if (!n) return false;
        if (!white(c)) { if (first==b) first=i; last=i+n; }
        i+=n;
    }
    if (first>=last || at<first || at>=last) return false;
    *start=first; *end=last; return true;
}
bool reader_sentence_span(const char *text,size_t len,size_t page_start,
                          size_t page_end,size_t at,size_t *start,size_t *end) {
    if (!text || !start || !end || page_start>=page_end || page_end>len ||
        at<page_start || at>=page_end) return false;
    for (size_t i=0;i<len;) {
        uint32_t c;
        size_t n=decode(text+i,len-i,&c);
        if (!n) return false;
        i+=n;
    }
    if (((unsigned char)text[page_start]&0xc0)==0x80 ||
        (page_end<len && ((unsigned char)text[page_end]&0xc0)==0x80) ||
        ((unsigned char)text[at]&0xc0)==0x80) return false;
    size_t begin=0;
    for (size_t i=0;i<len;) {
        uint32_t c;
        size_t n=decode(text+i,len-i,&c);
        bool line=newline(c);
        if (line || (terminal(c) && (c!='.' || !protected_period(text,len,i)))) {
            size_t finish=line ? i : i+n;
            size_t next=i+n;
            if (!line) {
                while (finish<len) {
                    uint32_t tail;
                    size_t tn=decode(text+finish,len-finish,&tail);
                    if (!terminal(tail) && !closer(tail)) break;
                    finish+=tn;
                }
                next=finish;
            }
            if (at>=begin && at<finish)
                return clipped(text,begin,finish,page_start,page_end,at,start,end);
            begin=next; i=next;
        } else i+=n;
    }
    return at>=begin && clipped(text,begin,len,page_start,page_end,at,start,end);
}

bool reader_sentence_whitespace(const char *text, size_t len) {
    if (!text && len) return false;
    for (size_t i=0;i<len;) {
        uint32_t c;
        size_t n=decode(text+i,len-i,&c);
        if (!n || !white(c)) return false;
        i+=n;
    }
    return true;
}
