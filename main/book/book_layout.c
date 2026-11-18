/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借鉴阅读演示的逐码点折行，建立 PSRAM 页表并绘制章节。
 * English: Adapt the reading demo's codepoint wrapping into PSRAM chapter pagination.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * 为尽早进入阅读，支持分批分页；未加载图片绘制可点击占位及已读重复提示，不在绘图时读取资源或解码。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 * Incremental pagination enables early reading; unloaded images show clickable placeholders and visited-chapter repeat hints; never read or decode resources while drawing.
 *
 * 冻结（本项目追加）：占位框可以整个关掉（book_layout_set_image_placeholders）。阅读器产品页不显示图片，
 * 提出者的要求是"能显示就显示，不能就直接不显示"，所以那里连占位框也不留；官方的图书演示页不受影响。
 * Frozen (added by this fork): placeholders can be turned off altogether (book_layout_set_image_placeholders).
 * The reader product page shows no images, and the requester's rule is "show them if you can, otherwise
 * show nothing", so it leaves no placeholder either; the vendor book demo page is unaffected.
 *
 * 冻结（本项目追加）：正文按《中文排版需求》（W3C clreq）里最要紧的几条排：
 *   1. 行首禁则：句读、右括号右引号不出现在行首；行尾禁则：左括号左引号不留在行尾。
 *   2. 标点挤压：相邻的全角标点、行尾的句读与右括号、行首的左括号各让出半个字宽的空白。
 *   3. 段首默认不缩进，靠段间距分段；原文自带的缩进空格去掉，所有段落顶格。
 *      （book_layout_set_indent_em 可以打开缩进，标题始终不缩进。）
 *   4. 两端对齐：折行的那些行把余下的空白匀到字间；段末行、标题不拉伸。
 *   另外，省略号、破折号不拆成两行。
 * 冻结（本项目追加）：英文按西文排版的通行做法排。原先英文单词一律不拆、空白匀到每个字母之间，
 * 真机上英文书的行被拉得很宽，提出者认为"非常差"，所以改成：
 *   5. 断词：一行放不下的英文单词按 TeX 的美式英语规则在音节处断开并加连字符（hyphen_en）；
 *      断点前至少 2 个字母、后至少 3 个；全大写、带数字、已经带连字符的词不断。
 *   6. 对齐只拉词间空格和汉字之间的空隙，单词内部的字母间距永远不动；每个空格最多加宽四分之三个字，
 *      汉字间最多三分之一个字，超过就不对齐、让右边参差（窄栏排英文的常规做法）。
 *   7. 词内的撇号（don't）不断；连字符、右引号不落在行首，左引号不留在行尾。
 *   8. 中文字体里的全角弯引号紧挨着西文时按墨迹的实际宽度占位，英文里不再出现 You’ ve 这样的空洞。
 * 每一行怎么排只由它的起始偏移决定，这样分页表仍然只存偏移，分页与绘制的结果一致。
 * Frozen (added by this fork): body text follows the few rules of the W3C Requirements for
 * Chinese Text Layout (clreq) that matter most:
 *   1. Line-start prohibition: pause/stop marks and closing brackets/quotes never start a line;
 *      line-end prohibition: opening brackets/quotes never end one.
 *   2. Punctuation compression: adjacent fullwidth marks, a line-final pause/stop or closing
 *      bracket, and a line-initial opening bracket each give up their half-em blank.
 *   3. No first-line indent by default; paragraphs are set apart by spacing, and indent spaces in
 *      the source are dropped so every paragraph starts flush left. (book_layout_set_indent_em
 *      turns an indent on; headings are never indented.)
 *   4. Justification: wrapped lines spread leftover space between characters; a paragraph's
 *      last line and headings are not stretched.
 *   Also, ellipses and dashes are not split.
 * Frozen (added by this fork): Latin text follows common Western practice. Words used to be kept whole
 * with the slack spread between every letter; on hardware, lines of English books were stretched wide
 * and the requester called it very poor, hence:
 *   5. Hyphenation: an English word that does not fit breaks at a syllable by the TeX American English
 *      patterns, with a hyphen (hyphen_en); at least 2 letters before the break and 3 after; all-caps
 *      words, words with digits and already hyphenated words are left whole.
 *   6. Justification stretches only word spaces and the gaps between Han characters, never the letter
 *      spacing inside a word; a space grows by at most three quarters of an em and a Han gap by a third, beyond which
 *      the line is left ragged (the usual choice for English in a narrow column).
 *   7. An apostrophe inside a word (don't) never breaks; hyphens and closing quotes do not start a line
 *      and opening quotes do not end one.
 *   8. A fullwidth curly quote from a Chinese font is fitted to its ink beside Latin text, so English no
 *      longer shows holes such as You’ ve.
 * A line's layout depends only on its start offset, so the page table still stores offsets only
 * and pagination agrees with drawing.
 */
#include "book_layout.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "hyphen_en.h"
#include "ttf_font.h"

#define PAGE_MAX 4096u
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static const char* s_text;
static size_t s_len;
static size_t* s_pages;
static size_t s_count;
static size_t s_capacity;
// 一行里的一个字。/ One glyph on a line.
typedef struct {
    uint32_t off;  ///< 在原文里的字节偏移 / Byte offset in the source text
    uint32_t cp;   ///< 码点 / Code point
    int16_t adv;   ///< 字体给的步进宽度 / Advance from the font
    int16_t cut;   ///< 因标点挤压从步进里减掉的宽度 / Width removed from the advance by punctuation compression
    int16_t dx;    ///< 画的时候往左挪多少（负数）；西文里的全角左引号用 / Draw offset, negative moves left; for a fullwidth opening quote in Latin text
    uint8_t n;     ///< UTF-8 字节数 / UTF-8 byte length
    uint8_t cls;   ///< glyph_class_t
} glyph_t;

// 排好的一行。/ A laid-out line.
typedef struct {
    size_t next;         ///< 下一行的起始偏移 / Start offset of the next line
    bool paragraph_end;  ///< 这一行后面是段落边界 / A paragraph boundary follows
    bool heading;
    int px;
    int count;           ///< 行内字数（含行尾空格）/ Glyphs on the line, trailing spaces included
    int visible;         ///< 去掉行尾空格后的字数 / Glyphs excluding trailing spaces
    int x0;              ///< 第一个字相对正文左边的位置（缩进，或行首左括号的挤压）/ First glyph offset from the body's left edge (indent, or line-start compression)
    int extra;           ///< 两端对齐时要匀出去的空白；不对齐为 0 / Space to spread when justified; 0 otherwise
    int weight;          ///< 这一行所有可拉伸空隙的权重之和 / Sum of the weights of the line's stretchable gaps
    int hyphen_adv;      ///< 行尾断词连字符的宽度；没有断词为 0 / Width of the line-final hyphen; 0 when the line does not hyphenate
} line_t;

// 单次排版/绘制内缓存步进；每个公开操作重置，换字体或字重不会读到旧值。
// Cache advances within one layout/draw operation; reset at each public operation so font/weight changes cannot go stale.
#define WIDTH_CACHE_SLOTS 256u
typedef struct {
    uint32_t cp;
    int px, advance;
} width_entry_t;
static width_entry_t s_widths[WIDTH_CACHE_SLOTS];

static int glyph_advance(uint32_t cp, int px, const char* text) {
    width_entry_t* entry = &s_widths[(cp * 2654435761u + (uint32_t)px) % WIDTH_CACHE_SLOTS];
    if (entry->px == px && entry->cp == cp) return entry->advance;
    const int advance = ttf_text_width_px(px, text);
    *entry = (width_entry_t){ .cp = cp, .px = px, .advance = advance };
    return advance;
}

static glyph_t* s_glyphs;
static size_t s_glyph_cap;
static int s_indent_em;  // 段首缩进几个字，默认 0 / first-line indent in ems, 0 by default

void book_layout_set_indent_em(int ems) { s_indent_em = ems < 0 ? 0 : ems; }
static bool s_hide_images;  // 没加载的图片不占位也不画 / unloaded images take no room and draw nothing
void book_layout_set_image_placeholders(bool show) { s_hide_images = !show; }
static EpdRect s_rect;
static int s_px;
// 正文字号下一个汉字的宽度。字号是行框的像素高，一个字的宽度通常只有它的七成左右，
// 所以"全角""半个字""缩进两字"都要按这个量，不能按字号。
// Width of one Han character at the body size. The size is the pixel height of the line box and a
// character is usually only about seven tenths as wide, so "fullwidth", "half an em" and "two-em
// indent" are measured against this, never against the size.
static int s_em;
static size_t s_scan;
static int64_t s_used;
static bool s_complete;

static const blk_t* s_blocks;
static size_t s_block_count;

// 块表是有序字节区间，二分查找当前行样式。/ Blocks are ordered byte ranges; binary-search the line style.
static const blk_t* block_at(size_t off) {
    if (!s_block_count) return NULL;
    size_t lo = 0, hi = s_block_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_blocks[mid].offset <= off) lo = mid;
        else hi = mid;
    }
    return &s_blocks[lo];
}
// 插图等比适配正文区域，不放大；整块换页，不切断图片。
// Fit illustrations proportionally without upscaling; move whole images across page boundaries.
static void image_size(const blk_t* block, int* width, int* height) {
    *width = block->image_width; *height = block->image_height;
    if (*width > s_rect.width) { *height = (int)((int64_t)*height * s_rect.width / *width); *width = s_rect.width; }
    if (*height > s_rect.height) { *width = (int)((int64_t)*width * s_rect.height / *height); *height = s_rect.height; }
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}
static int row_height(const blk_t* block, int px) {
    if (block && block->image) { int width, height; image_size(block, &width, &height); return height; }
    if (block && block->image_src) return s_hide_images ? 0 : 2 * s_px < s_rect.height ? 2 * s_px : s_rect.height;
    return px + px / 2;
}
static EpdRect placeholder_rect(EpdRect body, int top, int height) {
    int pad = body.width > 24 && height > 16 ? 8 : 0;
    return (EpdRect){body.x + pad, body.y + top + pad / 2, body.width - 2 * pad, height - pad};
}
static void draw_placeholder(uint8_t* fb, const blk_t* block, EpdRect rect) {
    for (int x = rect.x; x < rect.x + rect.width; ++x) {
        epd_draw_pixel(x, rect.y, 0, fb); epd_draw_pixel(x, rect.y + rect.height - 1, 0, fb);
    }
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        epd_draw_pixel(rect.x, y, 0, fb); epd_draw_pixel(rect.x + rect.width - 1, y, 0, fb);
    }
    int px = s_px < 32 ? s_px : 32;
    bool repeat = block->image_repeated && !block->image_title;
    const char* label = block->image_title ? "标题图 · 点击查看" : repeat ? "重复图片 · 点击查看" : "点击查看图片";
    if (ttf_text_width_px(px, label) > rect.width - 16) return;
    int lines = repeat ? 2 : 1;
    int top = rect.y + (rect.height - lines * (px + 4)) / 2;
    ttf_draw_text_px(fb, rect.x + rect.width / 2, top + ttf_ascender_px(px), px,
                     label, EPD_DRAW_ALIGN_CENTER, 0, 15);
    if (repeat) {
        char origin[64]; snprintf(origin, sizeof(origin), "已读最早：第 %u 节", (unsigned)block->image_first_chapter + 1);
        int small = px < 24 ? px : 24;
        if (ttf_text_width_px(small, origin) <= rect.width - 16)
            ttf_draw_text_px(fb, rect.x + rect.width / 2, top + px + 4 + ttf_ascender_px(small), small,
                             origin, EPD_DRAW_ALIGN_CENTER, 0, 15);
    }
}
// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {
    if (!remaining) return 0;
    const unsigned char* p = (const unsigned char*)text;
    if (p[0] > 0 && p[0] < 0x80) return 1;
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
               p[0] >= 0xe0 && p[0] <= 0xef ? 3 :
               p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; i++) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

void book_layout_free(void) {
    free(s_pages);
    free(s_glyphs);
    s_pages = NULL;
    s_glyphs = NULL;
    s_glyph_cap = 0;
    s_text = NULL;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_scan = 0;
    s_used = 0;
    s_complete = false;
    s_blocks = NULL;
    s_block_count = 0;
}

static bool append_page(size_t off) {
    if (s_count == PAGE_MAX) return false;
    if (s_count == s_capacity) {
        size_t cap = s_capacity ? s_capacity * 2 : 16;
        size_t* pages = heap_caps_realloc(s_pages, cap * sizeof(*pages), PSRAM_CAPS);
        if (!pages) return false;
        s_pages = pages;
        s_capacity = cap;
    }
    s_pages[s_count++] = off;
    return true;
}

/* ---- 中文排版规则 / Chinese layout rules ---- */

typedef enum {
    G_OTHER,        ///< 汉字及其他 / Han characters and everything else
    G_OPEN,         ///< 左括号、左引号：墨迹在右半，不能留在行尾 / Opening brackets and quotes: ink on the right half; may not end a line
    G_CLOSE,        ///< 句号逗号顿号分号冒号、右括号右引号：墨迹在左半，不能在行首 / Pause and stop marks, closing brackets and quotes: ink on the left half; may not start a line
    G_STOP,         ///< 问号叹号间隔号：不能在行首，但不挤压 / Question and exclamation marks, middle dot: may not start a line, never compressed
    G_NOSPLIT,      ///< 省略号、破折号：成对出现时不拆开 / Ellipsis and dash: a pair is never split
    G_ALNUM,        ///< 英文字母和数字：连续的一串不拆开 / Latin letters and digits: a run is never split
    G_SPACE,        ///< 空白 / Whitespace
    G_ASCII_OPEN,   ///< ( [ {
    G_ASCII_CLOSE,  ///< , . ; : ! ? ) ] } %
} glyph_class_t;

static uint32_t decode(const char* text, size_t n) {
    const unsigned char* p = (const unsigned char*)text;
    if (n == 1) return p[0];
    if (n == 2) return (uint32_t)(p[0] & 0x1f) << 6 | (p[1] & 0x3f);
    if (n == 3) return (uint32_t)(p[0] & 0x0f) << 12 | (uint32_t)(p[1] & 0x3f) << 6 | (p[2] & 0x3f);
    return (uint32_t)(p[0] & 0x07) << 18 | (uint32_t)(p[1] & 0x3f) << 12 | (uint32_t)(p[2] & 0x3f) << 6 | (p[3] & 0x3f);
}

static glyph_class_t classify(uint32_t cp) {
    if (cp < 0x80) {
        if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return G_ALNUM;
        if (cp == ' ' || cp == '\t') return G_SPACE;
        if (cp == '(' || cp == '[' || cp == '{') return G_ASCII_OPEN;
        // 连字符算在这一类：不落在行首，可以在它后面断（well-|known）。
        // The hyphen belongs here: it never starts a line, and a break may follow it (well-|known).
        if (cp && strchr(",.;:!?)]}%-", (int)cp)) return G_ASCII_CLOSE;
        return G_OTHER;
    }
    // 带重音的拉丁字母、希腊字母、俄文字母和英文字母一样，不从词中间断。
    // Accented Latin, Greek and Cyrillic letters behave like ASCII letters: no break inside a word.
    if ((cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || (cp >= 0x370 && cp <= 0x3FF) || (cp >= 0x400 && cp <= 0x52F)) {
        return G_ALNUM;
    }
    if (cp == 0x2010 || cp == 0x2013) return G_ASCII_CLOSE;  // ‐ –
    switch (cp) {
        case 0x00A0: case 0x3000:
            return G_SPACE;
        case 0x2018: case 0x201C: case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010:
        case 0x3014: case 0x3016: case 0xFF08: case 0xFF3B: case 0xFF5B:
            return G_OPEN;   // ‘ “ 〈 《 「 『 【 〔 〖 （ ［ ｛
        case 0x2019: case 0x201D: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011:
        case 0x3015: case 0x3017: case 0xFF09: case 0xFF3D: case 0xFF5D:
            return G_CLOSE;  // ’ ” 〉 》 」 』 】 〕 〗 ） ］ ｝
        case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B:
            return G_CLOSE;  // 、 。 ， ． ： ；
        case 0xFF01: case 0xFF1F: case 0x00B7: case 0x30FB:
            return G_STOP;   // ！ ？ · ・
        case 0x2014: case 0x2026:
            return G_NOSPLIT;  // — …
        default:
            return G_OTHER;
    }
}

static bool is_punct(uint8_t cls) { return cls == G_OPEN || cls == G_CLOSE || cls == G_STOP; }

// 只有占满一个字宽的标点才有半个字的空白可让。西文字体里的弯引号是窄的，不能再减。
// Only marks that fill a whole em have a half-em blank to give up. Curly quotes in Latin fonts are narrow and are left alone.
static bool is_fullwidth(const glyph_t* g, int em) { return g->adv * 4 >= em * 3; }

// a 后面紧跟 b 时，a 要不要让出半个字：a 是句读或右括号且 b 也是标点，或两个都是左括号。
// Whether a gives up half an em when b follows: a is a pause/stop or closing mark and b is punctuation, or both are opening marks.
static bool squeezes(const glyph_t* a, const glyph_t* b, int em) {
    if (!is_fullwidth(a, em)) return false;
    return (a->cls == G_CLOSE && is_punct(b->cls)) || (a->cls == G_OPEN && b->cls == G_OPEN);
}

// 能不能在 a 和 b 之间断行。/ Whether a line may break between a and b.
static bool can_break(const glyph_t* a, const glyph_t* b) {
    if (b->cls == G_CLOSE || b->cls == G_STOP || b->cls == G_ASCII_CLOSE) return false;  // 行首禁则 / line-start prohibition
    if (a->cls == G_OPEN || a->cls == G_ASCII_OPEN) return false;                        // 行尾禁则 / line-end prohibition
    if (a->cls == G_ALNUM && b->cls == G_ALNUM) return false;                            // 不拆单词 / keep words whole
    if (a->cls == G_NOSPLIT && b->cls == G_NOSPLIT && a->cp == b->cp) return false;     // 不拆省略号破折号 / keep …… and —— whole
    return true;
}

/* ---- 英文排版规则 / Latin layout rules ---- */

static bool ascii_letter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// 引号和撇号的角色要看上下文，只看原文里它前后的字节，所以同一个位置永远得到同一个结果。
// 词内的撇号（don't、it’s）当字母；直引号前面是空白或开头就是左引号，否则是右引号。
// Quotes and apostrophes are classified by context, read from the neighboring source bytes alone so a
// position always gets the same answer. An apostrophe inside a word (don't, it’s) counts as a letter; a
// straight quote after whitespace or at the start opens, otherwise it closes.
static glyph_class_t classify_at(uint32_t cp, size_t pos, size_t n) {
    if (cp != '\'' && cp != '"' && cp != 0x2019) return classify(cp);
    const unsigned char prev = pos > 0 ? (unsigned char)s_text[pos - 1] : ' ';
    const unsigned char next = pos + n < s_len ? (unsigned char)s_text[pos + n] : ' ';
    if (cp != '"' && ascii_letter(prev) && ascii_letter(next)) return G_ALNUM;
    if (cp == 0x2019) return G_CLOSE;
    const bool opens = prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r' || prev == '(' || prev == '[';
    return opens ? G_ASCII_OPEN : G_ASCII_CLOSE;
}

// 两个字之间的空隙在对齐时能拉多少：汉字（及全角标点）旁边的空隙权重 4，词间空格权重 9，
// 西文字母之间是 0，也就是不动。权重 w 的空隙最多加宽 w/12 个字：汉字间三分之一，空格四分之三。
// How far the gap between two glyphs may stretch when justifying: weight 4 beside a Han character or
// fullwidth mark, 9 for a word space, 0 between Latin letters, which never move. A gap of weight w grows
// by at most w/12 em: a third between Han characters, three quarters for a space.
#define GAP_WEIGHT_UNIT 12
static int gap_weight(const glyph_t* a, const glyph_t* b) {
    if (a->cp >= 0x2E80 || b->cp >= 0x2E80) return 4;
    return a->cls == G_SPACE ? 9 : 0;
}

// 行在第 k 个字处折断、而 k 开头的英文单词有一部分本来放得下时，看能不能把这个词断开，前一半留在
// 这一行。fits 是这一行放下的字数，width 是它们的总宽。返回留在这一行的字母数，0 表示不断。
// When the line breaks at glyph k and part of the English word starting there did fit, try to hyphenate
// the word and keep its first part on this line. fits is the number of glyphs that fit and width their
// total width. Returns how many letters stay on the line, 0 for no hyphenation.
static int hyphenate_at(int k, int fits, int64_t width, int avail, int px, int* hyphen_adv) {
    if (k <= 0 || k >= fits) return 0;
    // 前面紧挨着连字符的是复合词的后半，不再断第二次。/ Right after a hyphen is the second half of a compound; no second break.
    const uint32_t before = s_glyphs[k - 1].cp;
    if (before == '-' || before == 0x2010 || before == 0x2013) return 0;
    // 取出整个单词：从原文里一直读到不是字母为止。/ Collect the whole word from the source up to the first non-letter.
    const size_t start = s_glyphs[k].off;
    size_t len = 0;
    int upper = 0;
    while (start + len < s_len && ascii_letter((unsigned char)s_text[start + len])) {
        if (s_text[start + len] <= 'Z') upper++;
        if (++len > HYPHEN_EN_WORD_MAX) return 0;
    }
    if (len < 5) return 0;
    // 全大写或大小写混杂的多半是缩写、专名代号，不断；首字母大写的照常断。
    // All-caps or mixed-case words are mostly acronyms and codes and stay whole; an initial capital is fine.
    if (upper > 1 || (upper == 1 && s_text[start] > 'Z')) return 0;
    // 单词后面紧跟数字、撇号一类（也算词的一部分）时不断。/ Leave the word whole when a digit or apostrophe continues it.
    if (start + len < s_len) {
        const size_t n = codepoint_size(s_text + start + len, s_len - start - len);
        if (n && classify_at(decode(s_text + start + len, n), start + len, n) == G_ALNUM) return 0;
    }
    uint8_t breaks[HYPHEN_EN_WORD_MAX];
    if (hyphen_en_breaks(s_text + start, len, breaks) == 0) return 0;
    const int hyphen = glyph_advance('-', px, "-");
    if (hyphen <= 0) return 0;
    // 从放得下的最长一截往回找断点；连字符也要放得下。/ Walk back from the longest part that fit to a break; the hyphen must fit too.
    int64_t w = width;
    for (int p = fits - k; p >= 2; p--) {
        if (p < fits - k) w -= s_glyphs[k + p].adv;
        if ((size_t)p >= len || !breaks[p]) continue;
        if (w + hyphen > avail) continue;
        *hyphen_adv = hyphen;
        return p;
    }
    return 0;
}

static bool ensure_glyphs(size_t need) {
    if (need <= s_glyph_cap) return true;
    size_t cap = s_glyph_cap ? s_glyph_cap * 2 : 128;
    while (cap < need) cap *= 2;
    glyph_t* grown = heap_caps_realloc(s_glyphs, cap * sizeof(*grown), PSRAM_CAPS);
    if (!grown) return false;
    s_glyphs = grown;
    s_glyph_cap = cap;
    return true;
}

// 为了躲开禁则最多往回退这么多字；退不出合法断点就在原处硬断。
// Back off at most this many glyphs to dodge a prohibited break; with no legal break in reach, break where the line filled.
#define BACKTRACK_MAX 24

// 排从 off 开始的一行，结果放进 s_glyphs 和 line。保留原文字节位置；CRLF 算一个段落边界。
// Lay out the line starting at off into s_glyphs and line. Source offsets are preserved; CRLF is one paragraph boundary.
static bool take_line(size_t off, line_t* line) {
    const blk_t* block = block_at(off);
    memset(line, 0, sizeof(*line));
    line->heading = block && block->heading;
    line->px = s_px + (line->heading ? 8 : 0);
    const int px = line->px;
    size_t limit = block ? block->offset + block->len : s_len;
    if (block && (block->image || block->image_src)) {
        line->heading = false;
        line->next = limit + (limit < s_len && s_text[limit] == '\n');
        return line->next > off;
    }

    // 这一行是不是段落的第一行：只看前一个字节，所以同一个偏移永远得到同一个答案。
    // Whether this line opens a paragraph: decided by the preceding byte alone, so an offset always yields the same answer.
    const bool first = off == 0 || s_text[off - 1] == '\n' || s_text[off - 1] == '\r' || (block && off == block->offset);
    const bool body_first = first && !line->heading;
    size_t pos = off;
    // 段首怎么缩进由排版决定，原文自带的空格和全角空格一律去掉；续行开头的空格是上一行断行留下的，也不要。
    // The layout decides the first-line indent, so leading spaces (fullwidth included) in the source are
    // dropped; a space opening a continuation line is left over from the break and is dropped too.
    while (pos < limit && s_text[pos] != '\r' && s_text[pos] != '\n') {
        const size_t n = codepoint_size(s_text + pos, s_len - pos);
        if (!n) return false;
        const uint32_t cp = decode(s_text + pos, n);
        if (body_first ? classify(cp) != G_SPACE : (first || cp != ' ')) break;
        pos += n;
    }
    // 缩进只在开了的时候给，而且正文区太窄（不到八个字宽）时不给。
    // The indent applies only when turned on, and never when the body is narrower than eight ems.
    // 这一行的字宽：标题字号大一些，按比例放大。/ This line's em: headings are larger, scaled in proportion.
    const int em = s_px > 0 ? s_em * px / s_px : px;
    const bool indent = body_first && s_indent_em > 0 && s_rect.width >= 8 * em;
    if (indent) line->x0 = s_indent_em * em;
    const int avail = s_rect.width - line->x0;

    size_t end = pos;
    int64_t width = 0;
    int count = 0, lead = 0;
    glyph_t over = { 0 };
    bool overflow = false;
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        const size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        // 字体逐字取整后累加 advance；每个字只量一次。
        // Font advances are rounded per glyph and summed; each glyph is measured once.
        char text[5];
        memcpy(text, s_text + end, n);
        text[n] = 0;
        const uint32_t cp = decode(text, n);
        int adv = glyph_advance(cp, px, text);
        if (adv < 0 || adv > INT16_MAX) return false;
        glyph_t g = { .off = (uint32_t)end, .cp = cp, .adv = (int16_t)adv, .n = (uint8_t)n };
        g.cls = (uint8_t)classify_at(g.cp, end, n);
        // 中文字体里的弯引号占一整个字宽，夹在英文里会空出一大块（You’ ve）。紧挨着西文字符的弯引号
        // 收窄：右引号和撇号的墨迹在左边，直接收窄；左引号的墨迹在右边，画的时候往左挪。
        // Curly quotes in a Chinese font fill a whole em and leave a hole inside English (You’ ve). One
        // touching a Latin character is narrowed: a closing quote or apostrophe has its ink on the left and
        // simply narrows; an opening quote has its ink on the right and is drawn shifted left.
        if ((g.cp == 0x2018 || g.cp == 0x2019 || g.cp == 0x201C || g.cp == 0x201D) && is_fullwidth(&g, em)) {
            const bool opening = g.cp == 0x2018 || g.cp == 0x201C;
            const unsigned char beside = opening ? (end + n < s_len ? (unsigned char)s_text[end + n] : ' ')
                                                 : (end > 0 ? (unsigned char)s_text[end - 1] : ' ');
            if (beside > ' ' && beside < 0x80) {
                // 量得到墨迹就按墨迹收：墨迹两边各留一份它原来贴边那一侧的余量；量不到就按半个字。
                // With measurable ink, fit to it, keeping on both sides the margin the ink had on its tight
                // side; otherwise fall back to half an em.
                int ink_left = 0, ink_width = 0, shift = opening ? adv / 2 : 0, narrow = adv - adv / 2;
                if (ttf_glyph_ink_px(px, g.cp, &ink_left, &ink_width) && ink_left >= 0 && ink_left + ink_width <= adv) {
                    const int margin = opening ? adv - ink_left - ink_width : ink_left;
                    shift = opening ? ink_left - margin : 0;
                    narrow = ink_width + 2 * margin;
                }
                g.dx = (int16_t)-shift;
                adv = narrow;
                g.adv = (int16_t)adv;
            }
        }

        // 前一个字因为后面跟了这个标点而让出的宽度。/ Width the previous glyph gives up because this mark follows it.
        const int prev_cut = count > 0 && squeezes(&s_glyphs[count - 1], &g, em) ? s_glyphs[count - 1].adv / 2 : 0;
        // 一行以左括号、左引号开头时，它左边的空白挪到正文区外面，墨迹和左边缘对齐；有缩进的段首行除外。
        // An opening mark starting a line sheds its left blank so its ink meets the left edge; an indented first line is the exception.
        const int lead_cut = count == 0 && !indent && g.cls == G_OPEN && is_fullwidth(&g, em) ? adv / 2 : 0;
        // 句读和右括号落在行尾时，右边的空白不占正文宽度，所以往往挤得进来。
        // A pause/stop or closing mark at the end of a line does not count its right blank, so it usually still fits.
        const int tail_cut = g.cls == G_CLOSE && is_fullwidth(&g, em) ? adv / 2 : 0;
        const int64_t candidate = width - prev_cut + adv - lead_cut;
        if (candidate - tail_cut > avail) {
            if (count == 0) return false;
            over = g;
            overflow = true;
            break;
        }
        if (!ensure_glyphs((size_t)count + 1)) return false;
        if (prev_cut) s_glyphs[count - 1].cut = (int16_t)prev_cut;
        if (count == 0) lead = lead_cut;
        s_glyphs[count++] = g;
        width = candidate;
        end += n;
    }

    if (overflow) {
        // 行满了：从满的地方往回找第一个合法的断点（追出）。
        // The line is full: walk back from where it filled to the first legal break.
        int k = count;
        for (int j = count; j >= 1 && count - j <= BACKTRACK_MAX; j--) {
            if (can_break(&s_glyphs[j - 1], j < count ? &s_glyphs[j] : &over)) {
                k = j;
                break;
            }
        }
        const int part = hyphenate_at(k, count, width, avail, px, &line->hyphen_adv);
        if (part > 0) {
            // 英文单词断开：前一截留在这一行，行尾补连字符，后一截从下一行开始。
            // An English word hyphenates: its first part stays with a hyphen, the rest starts the next line.
            const int keep = k + part;
            end = keep < count ? s_glyphs[keep].off : over.off;
            count = keep;
        } else if (k < count) {
            // 被挪到下一行的那一截如果连一整行都放不下（比如一个超长的单词或网址），挪了也没用，
            // 还白白空出半行，那就不挪，在原处断。
            // If what would move down cannot fit on a whole line anyway (a very long word or URL), moving it
            // gains nothing and leaves half a line empty; break in place instead.
            int moved = over.adv;
            for (int i = k; i < count; i++) moved += s_glyphs[i].adv;
            if (moved <= s_rect.width) {
                end = s_glyphs[k].off;
                count = k;
            }
        }
        s_glyphs[count - 1].cut = 0;  // 行尾的字后面不再有邻居 / the last glyph has no neighbor on this line
    }

    line->count = count;
    line->visible = count;
    while (line->visible > 0 && s_glyphs[line->visible - 1].cp == ' ') line->visible--;
    line->x0 -= lead;
    line->next = end;
    if (end < s_len && (s_text[end] == '\r' || s_text[end] == '\n')) {
        line->next = end + 1;
        if (s_text[end] == '\r' && line->next < s_len && s_text[line->next] == '\n') line->next++;
        line->paragraph_end = true;
    }

    // 两端对齐：只拉伸因为放不下而折的行。空白太多（比如后面是个放不下的长单词）就不拉，免得字距松散。
    // Justify only lines that wrapped for lack of room. With too much slack (say a long word moved down)
    // leave the line ragged rather than loosen it.
    // 只拉词间空格和汉字旁边的空隙，单词里的字母间距不动（见 gap_weight）。
    // Only word spaces and gaps beside Han characters stretch; letter spacing inside a word stays put (see gap_weight).
    if (overflow && !line->heading && line->visible > 1) {
        int natural = -lead + line->hyphen_adv;
        for (int i = 0; i < line->visible; i++) natural += s_glyphs[i].adv - s_glyphs[i].cut;
        const glyph_t* last = &s_glyphs[line->visible - 1];
        if (last->cls == G_CLOSE && is_fullwidth(last, em)) natural -= last->adv / 2;
        for (int i = 0; i + 1 < line->visible; i++) line->weight += gap_weight(&s_glyphs[i], &s_glyphs[i + 1]);
        const int extra = avail - natural;
        if (extra > 0 && line->weight > 0 && extra * GAP_WEIGHT_UNIT <= line->weight * em) line->extra = extra;
    }
    return line->next > off;
}

// 把 take_line 排好的那一行画出来。/ Draw the line take_line just laid out.
static void draw_line(uint8_t* fb, const line_t* line, int left, int baseline) {
    int x = left + line->x0;
    int done = 0;  // 已经走过的空隙权重 / gap weight passed so far
    for (int i = 0; i < line->visible; i++) {
        const glyph_t* g = &s_glyphs[i];
        if (g->cls != G_SPACE) {
            char text[5];
            memcpy(text, s_text + g->off, g->n);
            text[g->n] = 0;
            ttf_draw_text_px(fb, x + g->dx, baseline, line->px, text, EPD_DRAW_ALIGN_LEFT, 0, 15);
        }
        x += g->adv - g->cut;
        // 余下的空白按权重分给各个空隙，除不尽的零头散开，不堆在行尾。
        // Leftover space is shared among the gaps by weight; the remainder is scattered rather than piled at the end.
        if (line->extra && i + 1 < line->visible) {
            const int w = gap_weight(g, &s_glyphs[i + 1]);
            x += line->extra * (done + w) / line->weight - line->extra * done / line->weight;
            done += w;
        }
    }
    if (line->hyphen_adv) ttf_draw_text_px(fb, x, baseline, line->px, "-", EPD_DRAW_ALIGN_LEFT, 0, 15);
}

bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px) {
    return book_layout_build_blocks(utf8, len, NULL, 0, rect, px);
}

bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    return book_layout_begin_blocks(utf8, len, blocks, count, rect, px) && book_layout_extend(PAGE_MAX);
}

bool book_layout_begin_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    book_layout_free();
    if ((!utf8 && len) || len == SIZE_MAX || px <= 0 || px > INT_MAX / 3 ||
        rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return false;
    int line_height = px + px / 2;
    if (line_height > rect.height) return false;
    for (size_t off = 0; off < len;) {
        size_t n = codepoint_size(utf8 + off, len - off);
        if (!n) return false;
        off += n;
    }
    if (count) {
        if (!blocks || count > HTML_TEXT_MAX_BLOCKS) return false;
        size_t expected = 0;
        for (size_t i = 0; i < count; ++i) {
            const blk_t* b = &blocks[i];
            if (b->image && (!b->image_width || !b->image_height)) return false;
            if (b->offset != expected || b->offset >= len || !b->len || b->len > len - b->offset ||
                ((unsigned char)utf8[b->offset] & 0xc0) == 0x80) return false;
            size_t end = b->offset + b->len;
            if (i + 1 < count) {
                if (end >= len || utf8[end] != '\n') return false;
                expected = end + 1;
            } else if (end != len) return false;
        }
    }
    s_blocks = blocks;
    s_block_count = count;
    s_text = utf8;
    s_len = len;
    s_px = px;
    // 字体里没有汉字（纯西文字体）时量不到，退回字号。/ A font without Han characters gives nothing to measure; fall back to the size.
    s_em = ttf_text_width_px(px, "国");
    if (s_em <= 0 || s_em > px) s_em = px;
    s_rect = rect;
    if (!append_page(0)) goto fail;
    return book_layout_extend(2);
fail:
    book_layout_free();
    return false;
}

bool book_layout_extend(size_t pages) {
    memset(s_widths, 0, sizeof(s_widths));
    if (!s_count) return false;
    if (!pages || s_complete) return true;
    size_t target = pages > PAGE_MAX - book_layout_page_count() ? PAGE_MAX : book_layout_page_count() + pages;
    while (s_scan < s_len) {
        line_t line;
        if (!take_line(s_scan, &line)) goto fail;
        const size_t next = line.next;
        const bool paragraph_end = line.paragraph_end, heading = line.heading;
        const blk_t* block = block_at(s_scan);
        int line_height = row_height(block, line.px);
        if (line_height > s_rect.height) goto fail;
        if (s_used + line_height > s_rect.height) {
            if (!append_page(s_scan)) goto fail;
            s_used = 0;
            if (book_layout_page_count() >= target) return true;
        }
        s_used += line_height;
        if (paragraph_end) s_used += line_height / (heading ? 2 : 3);
        s_scan = next;
    }
    s_complete = true;
    return true;
fail:
    book_layout_free();
    return false;
}

size_t book_layout_page_count(void) { return s_count ? s_count - !s_complete : 0; }
bool book_layout_complete(void) { return s_complete; }

size_t book_layout_page_start_offset(size_t page) {
    return page < s_count ? s_pages[page] : s_len;
}

size_t book_layout_page_for_offset(size_t off) {
    size_t lo = 0, hi = book_layout_page_count();
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_pages[mid] <= off) lo = mid;
        else hi = mid;
    }
    return lo;
}

void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px) {
    memset(s_widths, 0, sizeof(s_widths));
    if (!fb || page >= book_layout_page_count() || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        line_t line;
        if (!take_line(off, &line)) return;
        const size_t next = line.next;
        const bool paragraph_end = line.paragraph_end, heading = line.heading;
        const int line_px = line.px;
        const blk_t* block = block_at(off);
        int line_height = row_height(block, line_px);
        int image_width = 0;
        if (block && block->image) image_size(block, &image_width, &line_height);
        if (used + line_height > rect.height) return;
        if (image_width) {
            int left = rect.x + (rect.width - image_width) / 2;
            for (int y = 0; y < line_height; ++y) for (int x = 0; x < image_width; ++x) {
                size_t src = (size_t)((int64_t)y * block->image_height / line_height) * block->image_width +
                             (size_t)((int64_t)x * block->image_width / image_width);
                epd_draw_pixel(left + x, rect.y + (int)used + y, block->image[src], fb);
            }
        } else if (block && block->image_src) {
            if (!s_hide_images) draw_placeholder(fb, block, placeholder_rect(rect, (int)used, line_height));
        } else if (line.visible > 0) {
            draw_line(fb, &line, rect.x, rect.y + (int)used + ttf_ascender_px(line_px));
        }
        used += line_height;
        if (paragraph_end) used += line_height / (heading ? 2 : 3);
        off = next;
    }
}
size_t book_layout_image_at(size_t page, EpdRect rect, int x, int y, EpdRect* hit) {
    memset(s_widths, 0, sizeof(s_widths));
    if (page >= book_layout_page_count() || rect.width != s_rect.width || rect.height != s_rect.height ||
        rect.x < 0 || rect.y < 0 || rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height ||
        x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height) return SIZE_MAX;
    size_t off = s_pages[page], end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        line_t line;
        if (!take_line(off, &line)) return SIZE_MAX;
        const size_t next = line.next;
        const bool paragraph_end = line.paragraph_end, heading = line.heading;
        const blk_t* block = block_at(off);
        int height = row_height(block, line.px);
        if (used + height > rect.height) return SIZE_MAX;
        if (block && block->image_src && !block->image) {
            EpdRect area = placeholder_rect(rect, (int)used, height);
            if (x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height) {
                if (hit) *hit = area;
                return (size_t)(block - s_blocks);
            }
        }
        used += height;
        if (paragraph_end) used += height / (heading ? 2 : 3);
        off = next;
    }
    return SIZE_MAX;
}
