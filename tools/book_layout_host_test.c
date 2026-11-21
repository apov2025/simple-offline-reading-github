/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用伪字体验证布局完整性、偏移和测宽复杂度。
 * English: Verify layout completeness, offsets and measurement complexity using a fake font.
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
static char drawn[20000];
static size_t measured_codepoints;
static size_t measure_calls;
static int first_draw_px, last_draw_px;
static size_t image_pixels;
// 每次绘制调用的 x 坐标，用来检查缩进、挤压和两端对齐。/ The x of each draw call, to check indent, compression and justification.
static int draw_x[4096];
static size_t draw_n;
static int draw_y[4096], draw_px[4096];
static char draw_text[4096][5];
// 正文区 100 宽、15 高、字号 10：一行正好一页，页表就是各行的起始偏移。
// Body 100 wide, 15 high, size 10: one line per page, so the page table lists each line's start offset.
static void typeset(const char* text, uint8_t* fb) {
    const EpdRect body = {0, 0, 100, 15};
    assert(book_layout_build(text, strlen(text), body, 10));
    drawn[0] = 0;
    draw_n = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(fb, i, body, 10);
}
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* fb) {
    (void)color; (void)fb;
    assert(x >= 0 && y >= 0);
    ++image_pixels;
}
// 真字体里一个字的宽度比字号（行框的像素高）小；这个比例默认 100%，个别用例调小来模拟真字体。
// In a real font a character is narrower than the size (the line box's pixel height); the ratio is
// 100% by default and lowered by cases that imitate a real font.
static int glyph_scale_pct = 100;
static int ascender_scale_pct = 100;
static bool have_ink_bounds;
static int punctuation_fixture;
int ttf_text_width_px(int px, const char* text) {
    int n = 0, width = 0;
    const int em = px * glyph_scale_pct / 100;
    if (punctuation_fixture && !strcmp(text, "“")) return punctuation_fixture == 2 ? 10 : 5;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += *text == 'i' ? em / 2 : *text == 'W' ? em + em / 2 : em;
    }
    measured_codepoints += (size_t)n;
    measure_calls++;
    return width;
}
int ttf_ascender_px(int px) { return px * ascender_scale_pct / 100; }
// 主机测试里没有真字形：报"量不到"，排版退回"按半个字宽"的算法。
// No real glyphs on the host: report nothing measurable so the layout falls back to its half-em rule.
bool ttf_glyph_ink_px(int px, uint32_t codepoint, int* left, int* width) {
    if (punctuation_fixture) {
        if (codepoint == 0xFF08) { *left = 7; *width = 3; return true; }
        if (codepoint == 0x201C) {
            *left = punctuation_fixture == 3 ? -2 : 2;
            *width = 4; return true;
        }
        return false;
    }
    if (!have_ink_bounds) return false;
    *left = 2; *width = px - 3;
    return true;
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)y; (void)px; (void)align;
    assert(fg <= 15 && bg <= 15);
    if (draw_n < sizeof(draw_x) / sizeof(draw_x[0])) {
        draw_x[draw_n] = x; draw_y[draw_n] = y; draw_px[draw_n] = px;
        snprintf(draw_text[draw_n], sizeof(draw_text[draw_n]), "%.4s", text);
        draw_n++;
    }
    if (!drawn[0]) first_draw_px = px;
    last_draw_px = px;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
}
// 若选字重排、丢失字节或遗漏字形偏移，这些断言会失败。/ These assertions catch selection reflow, lost byte spans and ignored glyph offsets.
typedef struct { const char* text; size_t index, count, start, end, previous; } visit_check_t;
static bool check_glyph(const book_layout_glyph_t* g, void* context) {
    visit_check_t* c = context;
    size_t n = g->end - g->start;
    assert(g->start >= c->start && g->end <= c->end && g->start >= c->previous);
    assert(n > 0 && n <= 4);
    while (c->index < draw_n && !strcmp(draw_text[c->index], "-") &&
           (n != 1 || c->text[g->start] != '-')) c->index++;
    assert(c->index < draw_n);
    size_t i = c->index++;
    assert(strlen(draw_text[i]) == n && !memcmp(draw_text[i], c->text + g->start, n));
    assert(g->x == draw_x[i] && g->baseline == draw_y[i] && g->px == draw_px[i]);
    assert(g->rect.x == g->x + (have_ink_bounds ? 2 : 0) && g->rect.width > 0);
    const bool added_hyphen=i+1<draw_n && !strcmp(draw_text[i+1],"-") && draw_y[i+1]==g->baseline && c->text[g->end]!='-';
    if(added_hyphen) {
        const int right=draw_x[i+1]+(have_ink_bounds?g->px-1:ttf_text_width_px(g->px,"-"));
        assert(g->rect.x+g->rect.width>=right);
    } else if (have_ink_bounds) assert(g->rect.width == g->px - 3);
    assert(g->rect.y == g->baseline - g->px * ascender_scale_pct / 100 && g->rect.height == g->px);
    c->previous = g->end; c->count++;
    return true;
}
static bool stop_glyph(const book_layout_glyph_t* g, void* context) {
    (void)g; (*(size_t*)context)++; return false;
}
static size_t check_page_geometry(const char* text, EpdRect r, int px, size_t page) {
    uint8_t fb = 0;
    drawn[0] = 0; draw_n = 0;
    book_layout_draw_page(&fb, page, r, px);
    const size_t pages = book_layout_page_count(), count = draw_n;
    const bool complete = book_layout_complete();
    const size_t start = book_layout_page_start_offset(page), end = book_layout_page_start_offset(page + 1);
    visit_check_t check = {.text=text,.start=start,.end=end};
    assert(book_layout_visit_page(page, r, px, check_glyph, &check));
    while (check.index < draw_n && !strcmp(draw_text[check.index], "-")) check.index++;
    assert(check.index == count && draw_n == count);
    assert(book_layout_page_count() == pages && book_layout_complete() == complete);
    assert(book_layout_page_start_offset(page) == start && book_layout_page_start_offset(page + 1) == end);
    assert(!book_layout_visit_page(page, r, px + 1, check_glyph, &check));
    assert(!book_layout_visit_page(pages, r, px, check_glyph, &check));
    assert(!book_layout_visit_page(page, r, px, NULL, NULL));
    assert(!book_layout_visit_page(page, (EpdRect){r.x,r.y,r.width+1,r.height}, px, check_glyph, &check));
    if (check.count) {
        size_t stopped = 0;
        assert(!book_layout_visit_page(page, r, px, stop_glyph, &stopped));
        assert(stopped == 1);
    }
    return check.count;
}
static void test_page_geometry(void) {
    const char* cases[] = {"甲乙丙丁戊己庚辛壬癸，子丑", "甲：「乙」，丙", "“ab” ab’cd",
        "abcd computer xyz", "abc de fg hjklm", "abcdef don't x", "abcd 12345-67",
        "　　甲乙\r\n\r\n丙丁", ""};
    for (size_t c=0; c<sizeof(cases)/sizeof(cases[0]); c++) {
        EpdRect r = {37,29,100,45};
        assert(book_layout_build(cases[c], strlen(cases[c]), r, 10));
        for (size_t page=0; page<book_layout_page_count(); page++) check_page_geometry(cases[c],r,10,page);
    }
    const char* heading = "甲乙\n“body” computer typesetting\n末段";
    const char* last = strrchr(heading, '\n');
    blk_t blocks[] = {{.offset=0,.len=6,.heading=true}, {.offset=7,.len=(size_t)(last-heading)-7},
        {.offset=(size_t)(last-heading)+1,.len=strlen(last+1)}};
    glyph_scale_pct = 70;
    ascender_scale_pct = 80;
    have_ink_bounds = true;
    EpdRect r = {40,24,180,140};
    assert(book_layout_build_blocks(heading,strlen(heading),blocks,3,r,23));
    for (size_t page=0; page<book_layout_page_count(); page++) check_page_geometry(heading,r,23,page);
    glyph_scale_pct = ascender_scale_pct = 100;
    have_ink_bounds = false;
    char long_page[3001]; memset(long_page,'z',3000); long_page[3000]=0;
    r = (EpdRect){17,21,1000,450};
    assert(book_layout_begin_blocks(long_page,3000,NULL,0,r,10));
    assert(check_page_geometry(long_page,r,10,0) == 3000);
    // 不完成后续分页也能遍历已完成页。/ Completed pages can be visited without finishing the chapter.
    r = (EpdRect){17,21,100,30};
    assert(book_layout_begin_blocks(long_page,3000,NULL,0,r,10));
    assert(!book_layout_complete());
    assert(check_page_geometry(long_page,r,10,0) == 20);
}
// 窄引号、居左全角引号和负左边距都不能被前面的括号压住。
// A preceding parenthesis must not collide with narrow, left-aligned fullwidth, or negative-bearing quotes.
static void test_punctuation_ink(void) {
    uint8_t fb = 0;
    const char* cases[] = {"（“世界的黑暗大敌”）", "甲（“乙”）丙", "甲（（“乙”））丙"};
    for (punctuation_fixture = 1; punctuation_fixture <= 3; punctuation_fixture++) {
        for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
            typeset(cases[c], &fb);
            for (size_t i = 1; i < draw_n; i++) {
                if (strcmp(draw_text[i-1], "（") || strcmp(draw_text[i], "“")) continue;
                assert(draw_y[i-1] == draw_y[i]);
                const int left = punctuation_fixture == 3 ? -2 : 2;
                const int gap = draw_x[i] + left - (draw_x[i-1] + 10);
                // 原本无空白时仅禁止继续挤压，不凭空增加步进。
                // If the original pair has no blank, disable compression without adding advance.
                assert(gap == (left < 0 ? left : 1));
            }
        }
    }
    punctuation_fixture = 0;
}
int main(void) {
    EpdRect r = {0, 0, 20, 30};
    const char text[] = "甲乙丙丁戊己庚辛壬癸";
    assert(book_layout_build(text, strlen(text), r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 12);
    assert(book_layout_page_for_offset(11) == 0);
    assert(book_layout_page_for_offset(12) == 1);
    assert(book_layout_page_for_offset(999) == 2);
    uint8_t fb = 0;
    for (size_t i = 0; i < book_layout_page_count(); i++) {
        assert(book_layout_page_start_offset(i) % 3 == 0);
        book_layout_draw_page(&fb, i, r, 10);
    }
    assert(strcmp(drawn, text) == 0);
    const char mixed[] = "Wi甲iiW";
    assert(book_layout_build(mixed, strlen(mixed), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 7);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(strcmp(drawn, mixed) == 0);
    assert(book_layout_build("", 0, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_build(NULL, 0, r, 10));
    assert(!book_layout_build(NULL, 1, r, 10));
    assert(book_layout_page_count() == 0);
    assert(!book_layout_build("\xe7\x94", 2, r, 10));
    assert(!book_layout_build("x\0y", 3, r, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,5,30}, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,20,5}, 10));
    char* many = malloc(4097);
    memset(many, 'x', 4097);
    r = (EpdRect){0,0,10,15};
    assert(book_layout_build(many, 4096, r, 10));
    assert(book_layout_page_count() == 4096);
    assert(!book_layout_build(many, 4097, r, 10));
    assert(book_layout_page_count() == 0);
    free(many);
    assert(book_layout_build("a\r\n\r\nb", 6, (EpdRect){0,0,20,100}, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,20,100}, 10);
    assert(strcmp(drawn, "ab") == 0);
    char long_line[1024];
    memset(long_line, 'z', sizeof(long_line));
    r = (EpdRect){0,0,10000,30};
    measured_codepoints = measure_calls = 0;
    assert(book_layout_build(long_line, sizeof(long_line), r, 10));
    printf("long-line build: %zu calls, %zu measured codepoints\n", measure_calls, measured_codepoints);
    fflush(stdout);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    // 重复字符只需测量少数次，排版结果仍由下面的断言检查。/ Repeated glyphs need few measurements; layout assertions remain below.
    assert(measure_calls <= 4);
    // 同字号换字体后必须重新取宽度；绘制不能沿用旧字体的缓存。
    // A font change at the same size must remeasure; drawing cannot reuse the old font's cache.
    measured_codepoints = measure_calls = 0;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(strlen(drawn) == sizeof(long_line));
    assert(measure_calls <= 2);
    glyph_scale_pct = 80;
    measured_codepoints = measure_calls = 0;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measure_calls > 0 && measure_calls <= 2);
    assert(strlen(drawn) == sizeof(long_line));
    glyph_scale_pct = 100;

    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 11);
    assert(drawn[0] == 0);
    book_layout_draw_page(&fb, 99, r, 10);
    assert(drawn[0] == 0);
    measured_codepoints = measure_calls = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    // 重复字符只需测量少数次，排版结果仍由下面的断言检查。/ Repeated glyphs need few measurements; layout assertions remain below.
    assert(measure_calls <= 4);
    // 同字号换字体后必须重新取宽度；绘制不能沿用旧字体的缓存。
    // A font change at the same size must remeasure; drawing cannot reuse the old font's cache.
    measured_codepoints = measure_calls = 0;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(strlen(drawn) == sizeof(long_line));
    assert(measure_calls <= 2);
    glyph_scale_pct = 80;
    measured_codepoints = measure_calls = 0;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measure_calls > 0 && measure_calls <= 2);
    assert(strlen(drawn) == sizeof(long_line));
    glyph_scale_pct = 100;

    assert(strlen(drawn) == sizeof(long_line));
    assert(memcmp(drawn, long_line, sizeof(long_line)) == 0);
    assert(book_layout_page_start_offset(99) == sizeof(long_line));
    assert(!book_layout_build("\xed\xa0\x80", 3, r, 10));
    assert(!book_layout_build("\xf4\x90\x80\x80", 4, r, 10));
    assert(!book_layout_build("\xc0\xaf", 2, r, 10));
    book_layout_free();
    assert(book_layout_page_count() == 0);
    const char styled[] = "Title\nbody";
    blk_t blocks[] = {{.offset=0, .len=5, .heading=true}, {.offset=6, .len=4}};
    r = (EpdRect){0, 0, 100, 45};
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, "Titlebody") && first_draw_px == 18 && last_draw_px == 10);
    r.width = 20;
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 6);
    assert(book_layout_page_start_offset(5) == 6);
    drawn[0] = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(&fb, i, r, 10);
    assert(!strcmp(drawn, "Titlebody"));
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, (EpdRect){0,0,100,20}, 10));
    blocks[1].offset = 5;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 0);
    blocks[1].offset = 6;
    blocks[1].len = SIZE_MAX;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(!book_layout_build_blocks(styled, strlen(styled), NULL, 2, r, 10));
    blk_t split_utf8[] = {{.offset=0, .len=1, .heading=true}, {.offset=2, .len=2}};
    assert(!book_layout_build_blocks("甲\nx", 5, split_utf8, 2, r, 10));
    assert(book_layout_build("body", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_px == 10);
    // 分批与一次分页的边界相同，第一页不必测量整章。
    // Incremental and eager pagination agree; the first page never measures the whole chapter.
    char chapter[1201]; memset(chapter, 'x', 1200); chapter[1200] = 0;
    r = (EpdRect){0,0,40,30};
    assert(book_layout_build(chapter, 1200, r, 10));
    size_t pages = book_layout_page_count(), offsets[256];
    assert(pages < 256);
    for (size_t i=0; i<pages; ++i) offsets[i] = book_layout_page_start_offset(i);
    measured_codepoints = 0;
    assert(book_layout_begin_blocks(chapter, 1200, NULL, 0, r, 10));
    assert(!book_layout_complete() && book_layout_page_count() == 2 && measured_codepoints < 40);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(strlen(drawn) == 8);
    while (!book_layout_complete()) {
        size_t before = book_layout_page_count();
        assert(book_layout_extend(2));
        assert(book_layout_page_count() > before && book_layout_page_count() <= before + 2);
    }
    assert(book_layout_page_count() == pages);
    for (size_t i=0; i<pages; ++i) assert(book_layout_page_start_offset(i) == offsets[i]);
    assert(book_layout_page_for_offset(501) == 62);
    // 图片单块换页，文字不丢失；适配后仍在正文边界内。
    // Images cross pages atomically without losing text and fit inside the body.
    uint8_t pixels[2000]; memset(pixels, 128, sizeof(pixels));
    blk_t illustrated[] = {{.offset=0,.len=1}, {.offset=2,.len=3,.image=pixels,.image_width=40,.image_height=50}, {.offset=6,.len=1}};
    r = (EpdRect){0,0,20,30};
    assert(book_layout_build_blocks("a\nimg\nb", 7, illustrated, 3, r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 2 && book_layout_page_start_offset(2) == 6);
    drawn[0] = 0; image_pixels = 0;
    for (size_t i=0; i<3; ++i) book_layout_draw_page(&fb,i,r,10);
    assert(!strcmp(drawn,"ab") && image_pixels == 500);
    // 占位命中与绘图边界一致，不读取像素；重复提示不改变文本偏移。
    // Placeholder hits match drawing bounds without loading pixels; repeat hints preserve text offsets.
    illustrated[1].image = NULL; illustrated[1].image_src = "cover.jpg";
    r = (EpdRect){40,24,604,1000};
    assert(book_layout_build_blocks("a\nimg\nb", 7, illustrated, 3, r, 48));
    size_t before_pages = book_layout_page_count();
    size_t before_offset = book_layout_page_start_offset(0);
    EpdRect hit;
    assert(book_layout_image_at(0,r,100,150,&hit)==1);
    assert(hit.x==48 && hit.y>=r.y && hit.y+hit.height<=r.y+r.height);
    assert(book_layout_image_at(0,r,hit.x,hit.y,NULL)==1);
    assert(book_layout_image_at(0,r,hit.x+hit.width-1,hit.y+hit.height-1,NULL)==1);
    assert(book_layout_image_at(0,r,hit.x-1,hit.y,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,r,hit.x+hit.width,hit.y,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,r,hit.x,hit.y+hit.height,NULL)==SIZE_MAX);
    assert(book_layout_image_at(1,r,100,150,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,(EpdRect){40,24,600,1000},100,150,NULL)==SIZE_MAX);
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(strstr(drawn,"点击查看图片"));
    illustrated[1].image_repeated=true; illustrated[1].image_first_chapter=7;
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(strstr(drawn,"重复图片") && strstr(drawn,"第 8 节"));
    illustrated[1].image_title=true;
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(strstr(drawn,"标题图") && !strstr(drawn,"重复") && !strstr(drawn,"第 8 节"));
    assert(book_layout_page_count()==before_pages && book_layout_page_start_offset(0)==before_offset);
    // ---- 中文排版规则（clreq）/ Chinese layout rules (clreq) ----
    // 伪字体里每个字宽 10，所以全角标点让出的半个字宽是 5；一行可用 100，放 10 个字。
    // Every glyph is 10 wide in the fake font, so a fullwidth mark gives up 5; a line is 100 wide and holds 10 glyphs.

    // 默认不缩进：段落顶格，原文自带的全角空格去掉，也不画出来。
    // No indent by default: paragraphs start flush left; fullwidth spaces in the source are dropped and not drawn.
    typeset("\xe3\x80\x80\xe3\x80\x80甲乙", &fb);
    assert(!strcmp(drawn, "甲乙") && draw_x[0] == 0 && draw_x[1] == 10);
    // 打开缩进后是两个字，同样不和原文的空格叠加。
    // With the indent turned on it is two ems, still not added to the source's own spaces.
    book_layout_set_indent_em(2);
    typeset("\xe3\x80\x80\xe3\x80\x80甲乙", &fb);
    assert(!strcmp(drawn, "甲乙") && draw_x[0] == 20 && draw_x[1] == 30);
    book_layout_set_indent_em(0);

    // 行首禁则：逗号不能开头，连同前一个字一起挪到下一行；折了的那一行两端对齐，最后一个字顶到右边。
    // Line-start prohibition: a comma cannot open a line, so the glyph before it moves down with it;
    // the wrapped line is justified, its last glyph flush with the right edge.
    typeset("甲乙丙丁戊己庚辛壬癸，子丑", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 27);
    assert(draw_x[0] == 0 && draw_x[8] == 90);
    // 段末行不拉伸：字距还是 10。/ The paragraph's last line is not stretched: glyphs stay 10 apart.
    assert(draw_x[9] == 0 && draw_x[10] == 10 && draw_x[11] == 20 && draw_x[12] == 30);

    // 行尾挤压：逗号右边的空白不占宽度，所以它留在这一行的行尾。
    // Line-end compression: the comma's right blank does not count, so it stays at the end of this line.
    typeset("甲乙丙丁戊己庚辛壬，癸子", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 30);

    // 行尾禁则：左引号不能留在行尾，挪到下一行；一行以左引号开头时让出它左边的半个字。
    // Line-end prohibition: an opening quote cannot end a line and moves down; opening a line, it gives up its left half-em.
    typeset("甲乙丙丁戊己庚辛壬「癸子」", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 27);
    assert(draw_x[9] == -5 && draw_x[10] == 5);
    // 段首的左引号同样让出左边的半个字，墨迹和正文左边缘对齐。
    // An opening quote at the start of a paragraph gives up its left half-em too, so its ink meets the left edge.
    typeset("「甲」", &fb);
    assert(draw_x[0] == -5 && draw_x[1] == 5 && draw_x[2] == 15);

    // 标点挤压：冒号后面跟左引号、右引号后面跟逗号，前一个各让出半个字。
    // Punctuation compression: a colon before an opening quote and a closing quote before a comma each give up half an em.
    typeset("甲：「乙」，丙", &fb);
    assert(draw_n == 7 && draw_x[0] == 0 && draw_x[1] == 10 && draw_x[2] == 15 && draw_x[3] == 25);
    assert(draw_x[4] == 35 && draw_x[5] == 40 && draw_x[6] == 50);

    // 英文单词不从中间断开；空白太多的行不硬拉；续行不以空格开头。
    // A Latin word is not split; a line with too much slack is left ragged; a continuation line does not open with the space.
    typeset("甲乙丙丁戊己庚 hello", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 22);
    assert(draw_x[6] == 60 && draw_x[7] == 0);

    // 英文断词：放不下的单词在音节处断开，行尾补连字符，后半截从下一行开始。
    // Hyphenation: a word that does not fit breaks at a syllable with a hyphen; the rest starts the next line.
    typeset("abcd computer xyz", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 8);
    assert(!strcmp(drawn, "abcdcom-puterxyz"));
    assert(draw_x[4] == 50 && draw_x[6] == 70 && draw_x[7] == 80 && draw_x[8] == 0);

    // 英文对齐只拉词间空格，单词里的字母间距不动：10 的空白分给两个空格，各 5。
    // Latin justification stretches word spaces only, never letter spacing: 10 of slack goes to two spaces, 5 each.
    typeset("abc de fg hjklm", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 10);
    assert(draw_x[0] == 0 && draw_x[1] == 10 && draw_x[2] == 20);
    assert(draw_x[3] == 45 && draw_x[4] == 55 && draw_x[5] == 80 && draw_x[6] == 90);

    // 空格要拉得太宽（每个超过四分之三个字）就不对齐，右边参差。
    // When spaces would stretch too far (over three quarters of an em each) the line stays ragged.
    typeset("abc de 123456789", &fb);
    assert(book_layout_page_start_offset(1) == 7);
    assert(draw_x[3] == 40 && draw_x[4] == 50);

    // 词内的撇号不断开。/ An apostrophe inside a word never breaks.
    typeset("abcdef don't x", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 7);

    // 连字符不落在行首。/ A hyphen does not start a line.
    typeset("abcd 12345-67", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 5);

    // 直引号：空格后面的是左引号，不留在行尾。/ A straight quote after a space opens and does not end a line.
    typeset("abcdefgh \"xy\" z", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 9);

    // 全角弯引号挨着英文时只占半个字：撇号后面的字母紧跟上，左引号往左挪半个字。
    // A fullwidth curly quote beside Latin takes half an em: the letter after an apostrophe closes up, and an opening quote shifts left by half.
    typeset("ab’cd", &fb);
    assert(draw_n == 5 && draw_x[2] == 20 && draw_x[3] == 25 && draw_x[4] == 35);
    typeset("“ab”", &fb);
    assert(draw_n == 4 && draw_x[0] == -5 && draw_x[1] == 5 && draw_x[2] == 15 && draw_x[3] == 25);
    // 中文里的引号不受影响。/ Quotes in Chinese text are unaffected.
    typeset("甲“乙”丙", &fb);
    assert(draw_n == 5 && draw_x[1] == 10 && draw_x[2] == 20 && draw_x[3] == 30 && draw_x[4] == 40);

    // 真字体的字宽只有字号的七成左右：标点挤压和缩进都要按字宽算，不能按字号。
    // （按字号算的话，七成宽的标点不会被当成全角，挤压在真机上就不生效。）
    // A real font's character is about seven tenths of the size: compression and indent follow the
    // character width, not the size. (Measured against the size, a mark that wide would not count as
    // fullwidth and compression would silently do nothing on the device.)
    glyph_scale_pct = 70;
    typeset("甲：「乙」，丙", &fb);
    assert(draw_n == 7 && draw_x[0] == 0 && draw_x[1] == 7 && draw_x[2] == 11 && draw_x[3] == 18);
    assert(draw_x[4] == 25 && draw_x[5] == 29 && draw_x[6] == 36);
    book_layout_set_indent_em(2);
    typeset("甲乙", &fb);
    assert(draw_x[0] == 14 && draw_x[1] == 21);
    book_layout_set_indent_em(0);
    glyph_scale_pct = 100;

    // 省略号不拆成两行。/ An ellipsis is not split across lines.
    typeset("甲乙丙丁戊己庚辛壬……癸", &fb);
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 27);

    // 标题不缩进。/ Headings are not indented.
    blk_t heading_only[] = {{.offset=0, .len=6, .heading=true}};
    assert(book_layout_build_blocks("甲乙", 6, heading_only, 1, (EpdRect){0,0,200,60}, 10));
    draw_n = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,200,60}, 10);
    assert(draw_n == 2 && draw_x[0] == 0);

    book_layout_free();
    test_punctuation_ink();
    test_page_geometry();
    book_layout_free();
    puts("book_layout_host_test: PASS");
}
