/* SPDX-License-Identifier: Apache-2.0
 * 实际字体标点挤压回归：同时调用生产字体与布局，检查墨迹间距和版心。
 * Production-font punctuation regression: check ink spacing and body bounds with the real font and layout. */
#include <stdarg.h>
#include <time.h>
#include <stddef.h>
#include "../main/font/ttf_font.c"
const uint8_t builtin_ttf_start[1] asm("_binary_builtin_ttf_start")={0};
const uint8_t builtin_ttf_end[1] asm("_binary_builtin_ttf_end")={0};
void* heap_caps_malloc(size_t n,uint32_t caps){(void)caps;return malloc(n);}
void* heap_caps_calloc(size_t n,size_t s,uint32_t caps){(void)caps;return calloc(n,s);}
void heap_caps_free(void*p){free(p);}
void* heap_caps_realloc(void*p,size_t n,uint32_t caps){(void)caps;return realloc(p,n);}
size_t heap_caps_get_largest_free_block(uint32_t caps){(void)caps;return 8*1024*1024;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void sim_log(esp_log_level_t level,const char*tag,const char*fmt,...){(void)level;fprintf(stderr,"%s: ",tag);va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);}
esp_err_t book_store_flash_mount(void){return ESP_FAIL;}
const char* app_settings_font_path(void){return "builtin";}

#include <assert.h>
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t*fb){(void)x;(void)y;(void)color;(void)fb;}
#include "book_layout.h"
// 实际字体与原排版器共同验证墨迹，不把字宽伪装成字号。
// Exercise the production font and layout together; advances are not mocked as pixel heights.
static book_layout_glyph_t glyphs[128];
static size_t glyph_count;
static bool collect(const book_layout_glyph_t* g, void* ctx) {
    (void)ctx;
    assert(glyph_count < sizeof(glyphs)/sizeof(glyphs[0]));
    glyphs[glyph_count++] = *g;
    return true;
}
static void check_pair(const char* text, int px, int width) {
    EpdRect r = {20,20,width,500};
    assert(book_layout_build(text, strlen(text), r, px));
    for (size_t page = 0; page < book_layout_page_count(); page++) {
        glyph_count = 0;
        assert(book_layout_visit_page(page, r, px, collect, NULL));
        for (size_t i = 0; i < glyph_count; i++) {
            assert(glyphs[i].rect.x >= r.x);
            if (!i || glyphs[i-1].baseline != glyphs[i].baseline) continue;
            const book_layout_glyph_t *a = &glyphs[i-1], *b = &glyphs[i];
            // 右侧原本正常的引号括号仍沿用原步进；仅比较相对位置，允许前面的修复整体平移。
            // Preserve the already-correct closing pair's advance; preceding repairs may translate the whole pair.
            if (ttf_text_width_px(px, text) <= width && a->start > 0 && (unsigned char)text[a->start - 1] >= 0x80 &&
                !strncmp(text + a->start, "”", 3) && !strncmp(text + b->start, "）", 3)) {
                const int adv = ttf_text_width_px(px, "”");
                const int em = ttf_text_width_px(px, "国");
                const int expected = adv * 4 >= em * 3 ? adv - adv / 2 : adv;
                assert(b->x - a->x == expected);
            }
            if (strncmp(text + a->start, "（", 3) || strncmp(text + b->start, "“", 3)) continue;
            const int gap = b->rect.x - a->rect.x - a->rect.width;
            if (gap < 1) fprintf(stderr, "collision px=%d width=%d gap=%d text=%s\n", px, width, gap, text);
            assert(gap >= 1);
        }
    }
}
int main(int argc, char** argv) {
    assert(argc > 1);
    const char* cases[] = {"（“世界的黑暗大敌”）", "甲（“世界的黑暗大敌”）乙", "甲（（“乙”））丙", "甲“（乙）”丙", "甲）（乙丙", "甲）“乙”丙", "甲：（“ab”）乙"};
    for (int arg = 1; arg < argc; arg++) {
        assert(ttf_font_open(argv[arg]) == ESP_OK);
        for (int px = 24; px <= 72; px += 6) {
            for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
                check_pair(cases[c], px, 600);
                check_pair(cases[c], px, 160);
            }
        }
        book_layout_free();
        ttf_font_unload();
        printf("book punctuation real font: %s PASS (126 layouts, 24–72px, 160/600px columns)\n", argv[arg]);
    }
}
