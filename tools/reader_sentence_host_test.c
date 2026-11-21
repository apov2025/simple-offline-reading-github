/* SPDX-License-Identifier: Apache-2.0
 * 中文：原文字节范围与有限句界规则回归。/ English: Original-byte-span and bounded sentence-rule regressions.
 */
#include "reader_sentence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void span(const char *text, size_t ps, size_t pe, const char *hit, const char *want) {
    const char *where=strstr(text,hit), *expected=strstr(text,want);
    assert(where && expected);
    size_t start=999,end=999;
    assert(reader_sentence_span(text,strlen(text),ps,pe,(size_t)(where-text),&start,&end));
    assert(start==(size_t)(expected-text));
    assert(end==start+strlen(want));
    assert(start>=ps && end<=pe);
    assert(!memcmp(text+start,want,end-start));
}
static void whole(const char *text,const char *hit,const char *want) {
    span(text,0,strlen(text),hit,want);
}
static void invalid(const char *text,size_t len,size_t ps,size_t pe,size_t at) {
    size_t start=17,end=29;
    assert(!reader_sentence_span(text,len,ps,pe,at,&start,&end));
    assert(start==17 && end==29);
}
static void whitespace_tests(void) {
    assert(reader_sentence_whitespace(NULL,0));
    assert(reader_sentence_whitespace("",0));
    assert(!reader_sentence_whitespace(NULL,1));
    const char *spaces[]={" \t\r\n\v\f", "\xc2\xa0", "\xe2\x80\x80", "\xe2\x80\xaf", "\xe3\x80\x80"};
    for(size_t i=0;i<sizeof(spaces)/sizeof(spaces[0]);++i)
        assert(reader_sentence_whitespace(spaces[i],strlen(spaces[i])));
    const char *mixed=" \xc2\xa0\xe2\x80\x80\xe2\x80\xaf\xe3\x80\x80\n";
    assert(reader_sentence_whitespace(mixed,strlen(mixed)));
    assert(!reader_sentence_whitespace(" \xc2\xa0x",4));
    assert(!reader_sentence_whitespace("\xe2\x80\x8b",3));
    assert(!reader_sentence_whitespace("\xc0\xa0",2));
    assert(!reader_sentence_whitespace("\xed\xa0\x80",3));
    assert(!reader_sentence_whitespace("\xf4\x90\x80\x80",4));
    assert(!reader_sentence_whitespace("\xe3\x80",2));
    assert(!reader_sentence_whitespace("\x80",1));
    const char bounded[]={' ','\t','x'};
    assert(reader_sentence_whitespace(bounded,2));
    assert(!reader_sentence_whitespace(bounded,3));
}
static void chapter_span_tests(void) {
    // 不同字号模拟为不同分页截点；点击前、中、后页任意字均恢复同一个章节字节范围。
    // Simulate reflow with several page cuts; taps in any fragment restore the same chapter byte span.
    const char *text="前句。这句从前页开始，跨过当前页，直到后页结束！下一句。";
    const char *sentence="这句从前页开始，跨过当前页，直到后页结束！";
    size_t expected_start=(size_t)(strstr(text,sentence)-text);
    size_t expected_end=expected_start+strlen(sentence),len=strlen(text);
    const size_t page_glyphs[]={2,5,9};
    for(size_t layout=0;layout<sizeof(page_glyphs)/sizeof(page_glyphs[0]);++layout) {
        size_t width=page_glyphs[layout]*3;
        for(size_t at=expected_start;at<expected_end;at+=3) {
            size_t ps=(at/width)*width,pe=ps+width;
            if(pe>len)pe=len;
            size_t start=999,end=999;
            assert(reader_sentence_span(text,len,ps,pe,at,&start,&end));
            assert(start==(expected_start>ps?expected_start:ps));
            assert(end==(expected_end<pe?expected_end:pe));
            assert(reader_sentence_span(text,len,0,len,at,&start,&end));
            assert(start==expected_start && end==expected_end);
        }
    }
    whole("A preceding sentence. Mr. Smith's long sentence begins here and ends much later! Next.",
          "begins", "Mr. Smith's long sentence begins here and ends much later!");
    whole("A preceding sentence. Mr. Smith's long sentence begins here and ends much later! Next.",
          "later", "Mr. Smith's long sentence begins here and ends much later!");
}
int main(void) {
    whitespace_tests();
    chapter_span_tests();
    whole("他说：“第一句！”第二句。第三句？", "一", "他说：“第一句！”");
    whole("他说：“第一句！”第二句。第三句？", "二", "第二句。");
    whole("他说：“第一句！”第二句。第三句？", "三", "第三句？");
    whole("  ‘Wait!?’) Next. ","Wait","‘Wait!?’)");
    whole("[Why?!] (Yes.) Done!", "Yes", "(Yes.)");
    whole("先停……然后说。", "先", "先停……");
    whole("Wait... Next sentence.", "Next", "Next sentence.");
    whole("Wait... Next sentence.", "Wait", "Wait...");
    whole("No terminal\n\nNext paragraph\r\nLast", "paragraph", "Next paragraph");
    whole("\t\n　First sentence.　\tSecond!\n", "First", "First sentence.");
    whole("One. Two! Three?", "Two", "Two!");
    whole("值是3.14。下一句。", "14", "值是3.14。");
    whole("Mr. Smith met Dr. Jones. Next.", "Jones", "Mr. Smith met Dr. Jones.");
    whole("Mrs. Lee saw Prof. Wu on St. James St. today. Next.", "James", "Mrs. Lee saw Prof. Wu on St. James St. today.");
    whole("A vs. B, etc. are examples. Next.", "examples", "A vs. B, etc. are examples.");
    whole("Use e.g. apples, i.e. fruit. Next.", "fruit", "Use e.g. apples, i.e. fruit.");
    whole("The U.S. team saw J. R. Smith. Next.", "Smith", "The U.S. team saw J. R. Smith.");
    whole("😀开头，结尾！尾句。", "结尾", "😀开头，结尾！");
    whole("One. Two.", ". Two", "One.");
    const char *t="前句。跨页的长句还有后半。下一句。";
    size_t ps=(size_t)(strstr(t,"的长")-t),pe=(size_t)(strstr(t,"后半")-t);
    span(t,ps,pe,"还有","的长句还有");
    span(t,ps,strlen(t),"还有","的长句还有后半。");
    span(t,0,pe,"跨页","跨页的长句还有");
    const char *trim="甲。   beta gamma   后半。";
    ps=(size_t)(strstr(trim,"   beta")-trim);pe=(size_t)(strstr(trim,"后半")-trim);
    span(trim,ps,pe,"gamma","beta gamma");
    invalid(NULL,0,0,0,0);invalid("",0,0,0,0);
    invalid("abc",3,2,1,2);invalid("abc",3,0,4,0);
    invalid("abc",3,1,3,0);invalid("abc",3,0,3,3);
    invalid("abc",3,1,1,1);invalid(" \n ",3,0,3,0);
    invalid("甲乙",6,1,6,3);invalid("甲乙",6,0,5,0);invalid("甲乙",6,0,6,1);
    invalid("\xc0\xaf",2,0,2,0);invalid("\xed\xa0\x80",3,0,3,0);
    invalid("\xf4\x90\x80\x80",4,0,4,0);invalid("\xe4\xb8",2,0,2,0);
    invalid("a\x80",2,0,2,0);
    size_t untouched=123;
    assert(!reader_sentence_span("a",1,0,1,0,NULL,&untouched) && untouched==123);
    assert(!reader_sentence_span("a",1,0,1,0,&untouched,NULL) && untouched==123);
    // 非零页起点仍是原文章节偏移，不是页内重编号。/ Nonzero page starts retain chapter offsets rather than page-relative positions.
    const char *offset="prefix sentence. Target one! Tail.";
    ps=(size_t)(strstr(offset,"Target")-offset);
    span(offset,ps,strlen(offset),"one","Target one!");
    whole("他说（“真的？！”）后句。", "真", "他说（“真的？！”）");
    whole("First\r\nSecond", "Second", "Second");
    whole("Mr... Next.", "Mr", "Mr...");
    invalid("One.  Two.",10,0,10,4);
    invalid("One.\nTwo.",9,0,9,4);
    // 完整字节长度而非 NUL 终止决定输入；完整输入验证不能因提前找到句子而漏掉坏编码。
    // The supplied length, not NUL termination, bounds input; early sentences must not hide malformed later UTF-8.
    const char bounded[]={'A','!','Z'};
    size_t a=99,b=99;
    assert(reader_sentence_span(bounded,2,0,2,0,&a,&b) && a==0 && b==2);
    invalid("One. \xc0\xaf",7,0,4,0);
    // 长段落无固定长度截断；点选仍返回真实原文字节范围。/ Long paragraphs have no fixed truncation limit and preserve source offsets.
    static char long_text[131072];
    memset(long_text,'x',sizeof(long_text));
    long_text[sizeof(long_text)-1]='!';
    assert(reader_sentence_span(long_text,sizeof(long_text),65536,65599,65570,&a,&b));
    assert(a==65536 && b==65599);
    puts("reader_sentence: local sentence rules, UTF-8 boundaries and clipped source offsets passed");
    return 0;
}
