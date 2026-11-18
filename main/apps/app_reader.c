/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读器（产品页）：开机就是书库，点书名开读。只读 EPUB。
 * Pico 三键：左设置、中返回、右全刷；Metalio：左书库、中设置、右全刷。
 * Pico: settings/back/refresh; Metalio: home/settings/refresh.
 *
 * The reader (product page): boots into the library; tap a title to read.
 * EPUB only. Touch keys: left = reading settings panel, middle = back one
 * level until the library, right = one full refresh to clear ghosting.
 *
 * 冻结：书库是 EPUB 的文字列表，来源是卡上的 /sdcard/books（或 /sdcard/book）加机身自带的 /flash/books；
 * 不做封面、搜索、管理；排序见下面"最近阅读"一条。
 * Frozen: the library is a plain text list of EPUBs from /sdcard/books (or /sdcard/book) on the card plus
 * the built-in /flash/books; no covers, search or management; for ordering see the "most recently read" entry below.
 * 冻结：机身自带一个中英文都全的字体和几本书（storage 分区，见 builtin/），不插卡开机就能读。
 * 原先要求"用户自己备字体和书才能用"，提出者认为太麻烦，所以改成自带。"开始使用"引导页只在
 * 一本书都没有时出现（内置内容丢了又没插卡）。
 * Frozen: the device ships with one font covering Chinese and Latin plus a few books (the storage
 * partition, see builtin/), so it reads without a card. The earlier rule that users must bring their own
 * font and books was judged too much trouble by the requester. The "getting started" page appears only
 * when there is no book at all (built-in content lost and no card).
 * 冻结：书名、作者以 EPUB 自带的书目信息为准，文件名"书名 - 作者 (年份).epub"只作补充和年份的
 * 来源。原先只看文件名（为了开机快），真机上发现文件名里书名作者的先后各家不一，会把两者显示反，
 * 所以改了；为了不拖慢开机，只读当前这一页十本书的书目信息，并在重新列书时沿用已读过的。
 * Frozen: title and author come from the EPUB's own metadata; the file name "<title> - <author> (<year>).epub"
 * only fills gaps and supplies the year. Originally only the file name was used (for a fast boot), but
 * on hardware the order of title and author in file names varies by source and the two came out swapped.
 * To keep boot fast, only the ten books on the visible page are read, and results carry over a rescan.
 * 冻结：锁屏是软休眠，唤醒等于重新开机。锁屏时书还开着的话，开机后直接回到那本书、那一页，
 * 不停在书库；用户按中键回过书库，下次开机就停在书库。
 * Frozen: locking is a soft sleep and waking is a fresh boot. If a book was open when locking, the boot
 * goes straight back to that book and page instead of the library; once the user has gone back to the
 * library with the middle key, the next boot stays in the library.
 * 冻结：阅读器不整理书。去杂质、判断哪段是书名哪段是作者、修文件，都是电脑上的预处理程序
 * （tools/prepare_books.py，可以借助 AI）的事；设备只照书里自带的信息显示，读整理好的书。
 * Frozen: the reader does not tidy books. Stripping clutter, deciding which part is the title and which
 * the author, and repairing files belong to the desktop preprocessing tool (tools/prepare_books.py,
 * which may use AI); the device shows what the book carries and reads prepared books.
 * 冻结：书库按最近阅读排序（刚读过的在最前，没读过的按文件名排在后面）；这是提出者的要求，取代了
 * 原先"不排序、按文件名列出"的做法。底部左右两角是"上一页""下一页"，中间是"第几页/共几页"。
 * Frozen: the library is ordered by most recently read (the book just read first, unread ones after it by
 * file name); the requester asked for this, replacing the earlier plain file-name listing. The bottom
 * corners hold "previous" and "next" with "page/total" between them.
 * 冻结：书库里书名放不下一行就折行显示完整（最多三行），那一栏跟着变高，所以一页放几本不固定。
 * 原先超长的书名直接截断，提出者要求"长书名换行显示全"。
 * Frozen: a title too long for one line wraps so it shows in full (up to three lines) and its row grows,
 * so the number of books per page varies. Long titles used to be cut off; the requester asked for wrapping.
 * 冻结：只读文字，不支持图片（提出者："直接去掉对图片的支持吧，只用来读文字书"）。书里的图片不显示、
 * 不留占位，只有图片的章节直接跳过。
 * Frozen: text only, no image support (the requester: drop image support and read text books only). Images
 * in a book are neither shown nor given a placeholder, and chapters holding only images are skipped.
 * 冻结：读书时锁屏，屏幕上是这本书的封面；封面图由电脑上的整理程序提前做好放在书里（整屏灰度图），
 * 设备只管画，不解码图片。没整理过的书、在书库里锁屏，仍是内置的锁屏图。
 * Frozen: locking while reading leaves the book's cover on the screen. The cover is made ahead of time by
 * the desktop preparation tool and stored in the book (a full-screen gray image); the device only draws
 * it and decodes nothing. Unprepared books, and locking from the library, keep the built-in lock image.
 * 冻结：设置面板是一层平铺的选项（字号、字体、刷新），点一下即生效，不进子菜单。
 * Frozen: the settings panel is one flat sheet of choices (size, font, refresh); one tap applies, no submenus.
 * 冻结：屏幕上不画按钮和菜单把手；演示菜单只在书库页长按中键进入。
 * Frozen: no on-screen buttons or menu handle; the demo menu opens only by long-pressing the middle key in the library.
 * 冻结：Pico 不带截图，Metalio 按用户要求启用独占命名的 SD 截图。
 * Frozen: Pico screenshots remain off; Metalio saves exclusive SD screenshots at user request.
 * 原因：真机上试过：截图后卡会掉线。直接原因是写完后误调了 read_pico_sd_sync()
 * （它是"安全弹出卡"，不是"刷盘"）；另外有经验的人提醒过，墨水屏刷新和 TF 卡读写同时进行会互相
 * 干扰，官方代码里传书时也专门给屏幕扫描留了余量（display_set_bulk_io）。截图的代码留在
 * READER_SCREENSHOT 开关后面，恢复前要在真机上验证"先写完卡、再刷屏"。
 * Frozen: v1 ships without screenshots. On hardware the card dropped out after one. The direct cause
 * was calling read_pico_sd_sync() after the write (it safely ejects the card; it is not a flush). We
 * were also warned that an EPD refresh and TF card I/O interfere when they overlap, and the vendor
 * code does reserve scan margin during transfers (display_set_bulk_io). The code stays behind
 * READER_SCREENSHOT; before turning it back on, verify "finish the write, then refresh" on hardware.
 * 冻结：TF 卡只放用户内容：books 和 fonts 两个文件夹，缺了开机自动建；进度和设置存机身。
 * Frozen: the TF card holds user content only, in books and fonts, created at boot when missing;
 * progress and settings live on the device.
 * 冻结：默认字体是机身自带的那个；卡上的字体只是面板里的可选项，不再替用户自动挑卡上的字体
 * （已经存下的选择照旧生效）。卡拔掉时，卡上的字体退回机身字体，机身里的书可以接着读。
 * Frozen: the default font is the built-in one; card fonts are just choices in the panel and are no longer
 * auto-picked (a choice already saved still applies). When the card is pulled, a card font falls back to
 * the built-in font and a built-in book stays open.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "book_layout.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_epub.h"
#include "book_source.h"
#include "book_store.h"
#include "display.h"
#include "../board/reader_waveform.h"
#include "epdiy.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "html_text.h"

#include "../board/reader_media.h"
#include "settings.h"
#if READER_METALIO
#include "board/reader_board.h"
#include "board/reader_controls.h"
#include "board/reader_screenshot.h"
#endif
#include "sleep.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_menu.h"

static const char* TAG = "reader";

// 书的文件夹：卡上的单复数两种写法都认，最后一个是机身自带的。
// Book folders: both spellings on the card, then the built-in one.
static const char* const k_book_dirs[] = { "/sdcard/books", "/sdcard/book", "/flash/books" };
#define CARD_PREFIX "/sdcard/"
#define FLASH_PREFIX "/flash/"
#define BOOK_DIR_N ((int)(sizeof(k_book_dirs) / sizeof(k_book_dirs[0])))
#define FONTS_DIR "/sdcard/fonts"
#define SHOTS_DIR "/sdcard/screenshots"
// 截图功能的开关，第一版关着，原因见文件头。/ Screenshot switch, off in v1; see the file banner for why.
#if READER_METALIO
#define READER_SCREENSHOT 1
#else
#define READER_SCREENSHOT 0
#endif
#define LIB_MAX 512
#define NAME_BYTES 200
#define PATH_BYTES 288

// 书库版式。/ Library layout.
#define LIB_TOP 148
#define LIB_ROW_H 92
#define LIB_ROWS 10
#define LIB_PX 40
// 书名一行放不下就换行，最多三行；每多一行这本书的那一栏高出这么多。
// A title too long for one line wraps, up to three lines; each extra line makes the book's row this much taller.
#define LIB_TITLE_LINES 3
#define LIB_TITLE_LINE_H 48
#define LIB_BOTTOM (LIB_TOP + LIB_ROWS * LIB_ROW_H)

// 阅读版式：正文之下留一行页脚。/ Reading layout: one footer line below the body.
#define READ_TOP 40
#define READ_FOOTER_H 84
#define READ_FOOTER_PX 24
// 点屏幕左边这么宽是上一页，其余是下一页。/ A tap within this left strip turns back; elsewhere turns forward.
#define READ_PREV_W 228

// 设置面板。/ Settings panel.
#define SIZE_N 5
#define FONT_MAX 6
#define REFRESH_N 3
#define CHIP_H 76
#define LABEL_H 44
static const uint8_t k_sizes[SIZE_N] = { 40, 48, 56, 64, 72 };
static const char* const k_refresh_names[REFRESH_N] = { "快速", "均衡", "清晰" };

#define TOAST_MS 1600
#define SAVE_EVERY_TURNS 8

typedef enum { VIEW_LIBRARY, VIEW_READING } view_t;

typedef struct {
    char name[NAME_BYTES];  ///< 文件名，含扩展名 / File name with extension
    uint8_t dir;            ///< 在 k_book_dirs 里的下标 / Index into k_book_dirs
    uint8_t pct;            ///< 读到百分之几；没读过为 0 / Percent read; 0 when never opened
    bool started;           ///< 有阅读进度 / Has saved progress
    uint32_t recent;        ///< 阅读先后的序号，越大越近；没读过为 0 / Reading order, larger is more recent; 0 when never opened
    bool meta_ready;        ///< 下面三项已经定好 / The three fields below are settled
    char title[120];        ///< 显示用的书名 / Title as shown
    char author[72];        ///< 显示用的作者，可空 / Author as shown, may be empty
    char year[8];           ///< 年份，可空 / Year, may be empty
} lib_entry_t;

static view_t s_view;
static bool s_menu;        // 设置面板开着 / settings panel open
static bool s_force_full;  // 下一次整页用全刷 / next page present is a full refresh
static char s_notice[128];

static lib_entry_t* s_books;
static int s_book_n;
static int s_lib_page;
// 每一页从第几本书开始。书名换行后每栏高度不一，一页能放几本要排了才知道，所以翻到哪页记到哪页。
// First book of each page. With wrapped titles rows differ in height, so how many fit is known only
// after laying a page out; starts are recorded as pages are reached.
static uint16_t s_page_start[LIB_MAX + 2];
static int s_page_total;  // 一共几页；还没算出来是 0 / total pages; 0 until known
static int s_fill_index;  // 后台读书目信息读到第几本 / how far background metadata reading has got

static char s_path[PATH_BYTES];
static char s_book_title[NAME_BYTES];
static char s_chapter_title[160];
static uint32_t s_file_size;
static size_t s_chapter, s_page;
static int s_px = 48;
static char* s_text;
static size_t s_text_len;
static blk_t* s_blocks;
static size_t s_block_count;
static int s_unsaved_turns;

static bool s_booted;            // on_enter 已经跑过第一次 / on_enter has run once
static bool s_resume_pending;    // 开机后要回到上次那本书，还没办 / the last book is to be reopened after boot, not done yet
static bool s_card_missing;      // 书库是在没有卡的状态下画的 / the library was drawn without a card
static bool s_card_probing;      // 卡还在挂载中 / the card is still mounting
static int s_font_n;             // 卡上的字体数 / fonts found on the card
static uint8_t* s_cover;       // 当前这本书的封面图，锁屏时显示；没有为空 / the open book's cover for the lock screen; NULL when absent
static bool s_cover_pending;   // 书刚打开，封面还没去读 / the book just opened and its cover is not read yet
static char s_layout_font[TTF_FONT_PATH_MAX];  // 当前排版用的字体 / font the current layout was made with
static int64_t s_card_poll_ms;
static int64_t s_card_retry_after_ms;
static unsigned s_card_retry_count;
static int64_t s_toast_until_ms;
static int64_t s_work_started_us;  // 用户操作到刷新结束的计时 / user action through completed refresh
static int64_t s_idle_after_ms;    // 最近操作后短暂让出后台工作 / briefly defer background work after input
static bool s_mid_long_armed;  // 中键这次按下发生在书库且面板没开 / this middle press began in the library with the panel closed

/* ---- 小工具 / helpers ---- */

static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        // 截断时退到完整的 UTF-8 字符边界。/ Truncate back to a whole UTF-8 character.
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

// 超宽就从末尾按字删，直到放得下。/ Drop characters from the end until the text fits.
static void fit_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static bool has_epub_ext(const char* name) {
    const size_t n = strlen(name);
    return n > 5 && strcasecmp(name + n - 5, ".epub") == 0;
}


typedef struct {
    char title[NAME_BYTES];
    char author[NAME_BYTES];
    char year[8];
} book_meta_t;

// 从文件名拆出书名、作者、年份。约定："书名 - 作者 (年份).epub"，作者和年份都可以省略，
// 年份的括号用半角全角都行。拆不出来的部分留空，整个文件名当书名。只在书里没有自带信息时用得上。
// Split a file name into title, author and year. Convention: "<title> - <author> (<year>).epub"; author
// and year are both optional and the year's brackets may be ASCII or fullwidth. Missing parts stay
// empty and the whole name is the title. Used only when the book carries no metadata of its own.
static void meta_from_name(const char* name, book_meta_t* meta) {
    memset(meta, 0, sizeof(*meta));
    char stem[NAME_BYTES];
    copy_text(stem, sizeof(stem), name);
    size_t n = strlen(stem);
    if (n > 5 && strcasecmp(stem + n - 5, ".epub") == 0) stem[n -= 5] = 0;

    // 末尾的 (2008) 或 （2008）。/ A trailing (2008) or （2008）.
    static const struct { const char* open; const char* close; } brackets[] = { { "(", ")" }, { "（", "）" } };
    for (size_t b = 0; b < sizeof(brackets) / sizeof(brackets[0]); b++) {
        const size_t lo = strlen(brackets[b].open), lc = strlen(brackets[b].close);
        if (n < lo + 4 + lc || strcmp(stem + n - lc, brackets[b].close) != 0) continue;
        const char* digits = stem + n - lc - 4;
        if (strncmp(digits - lo, brackets[b].open, lo) != 0) continue;
        if (!isdigit((unsigned char)digits[0]) || !isdigit((unsigned char)digits[1])
            || !isdigit((unsigned char)digits[2]) || !isdigit((unsigned char)digits[3])) continue;
        memcpy(meta->year, digits, 4);
        n -= lo + 4 + lc;
        while (n > 0 && stem[n - 1] == ' ') n--;
        stem[n] = 0;
        break;
    }

    // 最后一个" - "右边是作者。/ Whatever follows the last " - " is the author.
    char* split = NULL;
    for (char* p = strstr(stem, " - "); p != NULL; p = strstr(p + 3, " - ")) split = p;
    if (split != NULL && split != stem && split[3] != 0) {
        *split = 0;
        copy_text(meta->author, sizeof(meta->author), split + 3);
    }
    copy_text(meta->title, sizeof(meta->title), stem);
}

static bool card_ready(void) {
    reader_media_info_t sd = { 0 };
    return reader_media_get_info(&sd) == ESP_OK && sd.present && sd.mounted;
}

// 开机后卡要过一会儿才挂载好，这段时间不能说"没有卡"。
// The card takes a moment to mount after boot; during that time it must not be reported as absent.
static bool card_probing(void) {
    reader_media_info_t sd = { 0 };
    return reader_media_get_info(&sd) == ESP_ERR_NOT_FINISHED;
}

// 当前字体是不是中文英文都有。/ Whether the open font covers both Chinese and Latin text.
// 嵌在固件里的界面小字体不算：它只有界面用到的那些字，这四个字碰巧都有，拿来读书照样缺字。
// The UI subset embedded in the firmware does not count: it holds only the characters the UI uses,
// happens to include these four, and still leaves a book full of gaps.
static bool font_covers_both(void) {
    if (ttf_font_is_builtin()) return false;
    return ttf_font_has_glyph(0x7684) && ttf_font_has_glyph(0x4E00) && ttf_font_has_glyph('a') && ttf_font_has_glyph('A');
}

static bool s_font_checked;  // 已经找过一遍可用字体了 / usable fonts have already been searched once

static bool on_card(const char* path) { return strncmp(path, CARD_PREFIX, sizeof(CARD_PREFIX) - 1) == 0; }
static bool on_flash(const char* path) { return strncmp(path, FLASH_PREFIX, sizeof(FLASH_PREFIX) - 1) == 0; }

// 默认字体是机身自带的那个。固件里嵌的小字体只够画界面，读书会缺字，所以当前字体中英文不全时
// 换成能用的：先找机身的，再找卡上的。用户存过卡上的字体而卡还没就绪时只是临时顶替、不改设置，
// 卡好了主循环会换回去。
// The default font is the built-in one. The small font embedded in the firmware only covers the UI, so
// when the open font lacks Chinese or Latin a usable one replaces it: built-in first, then the card.
// If a card font is saved but the card is not ready, the replacement is temporary and the setting is
// kept; the loop switches back once the card is up.
static void ensure_reading_font(void) {
    if (font_covers_both() || s_font_checked) return;
    const int n = ttf_font_count();
    if (n <= 0) return;
    s_font_checked = true;
    const bool keep_setting = on_card(app_settings_font_path());
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            const ttf_font_item_t* item = ttf_font_item(i);
            if (item == NULL || on_flash(item->path) != (pass == 0)) continue;
            char path[TTF_FONT_PATH_MAX];
            copy_text(path, sizeof(path), item->path);
            if (ttf_font_open(path) == ESP_OK && font_covers_both()) {
                if (!keep_setting) app_settings_set_font_path(path);
                ESP_LOGI(TAG, "reading font %s%s", path, keep_setting ? " (temporary)" : "");
                return;
            }
        }
    }
}

static EpdRect body_rect(void) {
    return (EpdRect){ UI_MARGIN, READ_TOP, ui_content_width(), UI_LOCK_HEIGHT - READ_TOP - READ_FOOTER_H };
}

static EpdRect toast_rect(void) {
    return (EpdRect){ UI_MARGIN, UI_LOCK_HEIGHT - 176, ui_content_width(), 76 };
}

// 在画面上盖一条提示并只把这一条推上屏（快刷）。
// Lay a notice strip over the page and push just that strip (fast update).
static void show_toast(app_ctx_t* ctx, const char* text) {
    const EpdRect r = toast_rect();
    char line[160];
    copy_text(line, sizeof(line), text);
    fit_text(line, UI_PX_CAPTION, r.width - 2 * UI_PAD);
    ui_fill_round_rect(ctx->fb, r, UI_CHIP_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(ctx->fb, r, UI_CHIP_RADIUS, UI_GRAY_BLACK);
    ui_text_vc(ctx->fb, r.x + r.width / 2, r.y + r.height / 2, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_CENTER, false);
    guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &READER_WAVEFORM, MODE_DU, r));
    s_toast_until_ms = ctx->now_ms + TOAST_MS;
}

/* ---- 书库 / library ---- */

// 最近读过的排在前面；没读过的在后面，按文件名排。
// Most recently read first; unread books follow in file-name order.
static int compare_entries(const void* a, const void* b) {
    const lib_entry_t* x = a;
    const lib_entry_t* y = b;
    if (x->recent != y->recent) return x->recent > y->recent ? -1 : 1;
    return strcmp(x->name, y->name);
}

static bool junk_meta(const char* text) {
    return !text[0] || strcasecmp(text, "unknown") == 0 || strcasecmp(text, "untitled") == 0 || strcmp(text, "未知") == 0;
}

// 定下一本书显示用的书名、作者、年份。以 EPUB 里的书目信息为准；它缺哪项就用文件名补哪项。
// 文件名两段里有一段和书名相同，另一段就是作者，这样"作者 - 书名"的文件名也不会显示反。
// 年份优先用文件名里的：EPUB 里的日期常常是电子版的制作日期。
// Settle the title, author and year shown for a book. EPUB metadata leads; the file name fills what it
// lacks. When one of the file name's two parts equals the title, the other is the author, so an
// "<author> - <title>" name does not come out swapped. The file name's year wins because an EPUB date
// is often when the e-book was produced.
static void entry_load_meta_from(lib_entry_t* item, bool current_book) {
    if (item->meta_ready) return;
    item->meta_ready = true;
    book_meta_t name;
    meta_from_name(item->name, &name);
    book_epub_meta_t in;
    char path[PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%s", k_book_dirs[item->dir], item->name);
    if ((current_book ? book_read_meta(&in) : book_epub_read_meta(path, &in)) != ESP_OK) memset(&in, 0, sizeof(in));
    // 整理书目信息（去杂质、判断谁是作者）是电脑上预处理程序的事，这里原样显示，只挡掉 0101 这种假年份。
    // Tidying metadata (clutter, who the author is) belongs to the desktop preprocessing tool; here it is shown as is, apart from rejecting placeholder years such as 0101.
    if (atoi(in.year) < 1000 || atoi(in.year) > 2100) in.year[0] = 0;
    const bool has_title = !junk_meta(in.title), has_author = !junk_meta(in.author);
    const char* title = has_title ? in.title : name.title;
    const char* author = has_author ? in.author : name.author;
    if (has_title && !has_author && name.author[0] && strcmp(name.author, in.title) == 0) author = name.title;
    if (!has_title && has_author && name.author[0] && strcmp(name.title, in.author) == 0) title = name.author;
    if (strcmp(title, author) == 0) author = "";
    copy_text(item->title, sizeof(item->title), title);
    copy_text(item->author, sizeof(item->author), author);
    copy_text(item->year, sizeof(item->year), name.year[0] ? name.year : in.year);
}

static void entry_load_meta(lib_entry_t* item) {
    entry_load_meta_from(item, false);
}

// 一本书显示用的书名、作者、年份：读过书目信息就用它，没读过先按文件名。
// Title, author and year as shown: from the metadata once read, otherwise from the file name.
static void entry_meta(const lib_entry_t* item, book_meta_t* meta) {
    if (!item->meta_ready) {
        meta_from_name(item->name, meta);
        return;
    }
    memset(meta, 0, sizeof(*meta));
    copy_text(meta->title, sizeof(meta->title), item->title);
    copy_text(meta->author, sizeof(meta->author), item->author);
    copy_text(meta->year, sizeof(meta->year), item->year);
}

static int entry_text_width(const lib_entry_t* item) {
    return ui_content_width() - (item->started ? 96 : 0);
}

static uint32_t utf8_at(const char* p, size_t* n) {
    const unsigned char c = (unsigned char)p[0];
    *n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    for (size_t i = 1; i < *n; i++) {
        if (p[i] == 0) { *n = i; break; }
    }
    if (*n == 1) return c;
    if (*n == 2) return (uint32_t)(c & 0x1f) << 6 | ((unsigned char)p[1] & 0x3f);
    if (*n == 3) return (uint32_t)(c & 0x0f) << 12 | (uint32_t)((unsigned char)p[1] & 0x3f) << 6 | ((unsigned char)p[2] & 0x3f);
    return 0x10000;
}

// 把书名按宽度折成几行：英文在空格处折，中文在字间折。最后一行还放不下就截掉。返回行数。
// Wrap a title to the width: Latin breaks at spaces, Chinese between characters. What still does not
// fit on the last line is cut. Returns the number of lines.
static int wrap_title(const char* text, int px, int width, char lines[LIB_TITLE_LINES][NAME_BYTES]) {
    int count = 0;
    const char* p = text;
    while (*p && count < LIB_TITLE_LINES) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* line = lines[count];
        size_t len = 0, brk = 0;
        const char* q = p;
        const char* brk_q = p;
        bool full = false;
        while (*q) {
            size_t n;
            const uint32_t cp = utf8_at(q, &n);
            if (len + n >= NAME_BYTES) { full = true; break; }
            memcpy(line + len, q, n);
            line[len + n] = 0;
            if (ttf_text_width_px(px, line) > width) {
                line[len] = 0;
                full = true;
                break;
            }
            len += n;
            q += n;
            size_t next_n;
            const uint32_t next = *q ? utf8_at(q, &next_n) : 0;
            // 可以在这个字后面折：它是空格或连字符，或者它或下一个字是汉字。
            // A break may follow this character: it is a space or hyphen, or it or the next one is Han.
            if (cp == ' ' || cp == '-' || cp >= 0x2E80 || next >= 0x2E80) {
                brk = len;
                brk_q = q;
            }
        }
        if (full && brk > 0 && count + 1 < LIB_TITLE_LINES) {
            len = brk;
            q = brk_q;
        }
        line[len] = 0;
        while (len > 0 && line[len - 1] == ' ') line[--len] = 0;
        if (len == 0) break;
        count++;
        p = q;
    }
    return count > 0 ? count : 1;
}

static int entry_height(const lib_entry_t* item) {
    book_meta_t meta;
    char lines[LIB_TITLE_LINES][NAME_BYTES];
    entry_meta(item, &meta);
    lines[0][0] = 0;
    return LIB_ROW_H + (wrap_title(meta.title, LIB_PX, entry_text_width(item), lines) - 1) * LIB_TITLE_LINE_H;
}

typedef struct {
    int index;  ///< 第几本书 / Book index
    int y;      ///< 这一栏的顶 / Top of the row
    int h;      ///< 这一栏的高 / Height of the row
} lib_row_t;

// 当前页放得下哪几本书。画和点共用这一份。/ Which books fit on the current page; drawing and hit-testing share this.
static int page_rows(lib_row_t rows[LIB_ROWS]) {
    int y = LIB_TOP, n = 0;
    for (int i = s_page_start[s_lib_page]; i < s_book_n && n < LIB_ROWS; i++) {
        const int h = entry_height(&s_books[i]);
        if (n > 0 && y + h > LIB_BOTTOM) break;
        rows[n++] = (lib_row_t){ i, y, h };
        y += h;
    }
    return n;
}

static bool page_has_next(void) { return s_page_start[s_lib_page + 1] < s_book_n; }

// 准备当前这一页：只给这一页放得下的书读书目信息（翻到哪页读哪页），并记下下一页从哪本开始。
// Prepare the current page: read metadata only for the books that fit on it (page by page) and record
// where the next page starts.
static void prepare_page(void) {
    if (s_books == NULL || s_resume_pending) return;
    int y = LIB_TOP, n = 0, i = s_page_start[s_lib_page];
    for (; i < s_book_n && n < LIB_ROWS; i++) {
        entry_load_meta(&s_books[i]);
        const int h = entry_height(&s_books[i]);
        if (n > 0 && y + h > LIB_BOTTOM) break;
        y += h;
        n++;
    }
    s_page_start[s_lib_page + 1] = (uint16_t)i;
}

static bool library_idle_step(void);

// 从第一页重新排到第 want 页（列表或字体变了以后，各页的起点都要重算）。
// Lay pages out again from the first up to page want (page starts all change with the list or the font).
static void pages_rebuild(int want) {
    s_lib_page = 0;
    s_page_start[0] = 0;
    s_page_start[1] = 0;
    s_page_total = 0;
    s_fill_index = 0;
    if (s_books == NULL || s_resume_pending) return;
    prepare_page();
    while (s_lib_page < want && page_has_next()) {
        s_lib_page++;
        prepare_page();
    }
    // 书目信息都已经读过（重新列书时沿用下来的）：当场把总页数算出来，页脚不用事后再补刷一次。
    // With every book's metadata already read (carried over a rescan) the total is worked out on the spot, so the footer needs no later repaint.
    bool all = true;
    for (int i = 0; i < s_book_n && all; i++) all = s_books[i].meta_ready;
    if (all) (void)library_idle_step();
}

static void library_scan(void) {
    // 重新列书前留下旧的一份，读过的书目信息接着用，不用再拆一遍 EPUB。
    // Keep the old list across a rescan so metadata already read is reused instead of reopening EPUBs.
    lib_entry_t* old = NULL;
    const int old_n = s_book_n;
    if (s_books != NULL && old_n > 0) {
        old = malloc((size_t)old_n * sizeof(*old));
        if (old != NULL) memcpy(old, s_books, (size_t)old_n * sizeof(*old));
    }
    s_book_n = 0;
    s_notice[0] = 0;
    s_card_missing = !card_ready();
    s_card_probing = s_card_missing && card_probing();
    if (!s_card_missing) {
        s_card_retry_count = 0;
        s_card_retry_after_ms = 0;
        // 卡恢复后先恢复用户选的字体，再排书库，避免先用默认字体多刷一帧。
        // Restore the saved font before laying out the recovered library, avoiding an intermediate fallback-font frame.
        const char* saved_font = app_settings_font_path();
        if (on_card(saved_font) && (!ttf_font_ready() || strcmp(ttf_font_path(), saved_font) != 0)) {
            (void)ttf_font_open(saved_font);
        }
    }
    (void)book_store_flash_mount();
    s_font_n = 0;
    const int fonts = ttf_font_scan();
    for (int i = 0; i < fonts; i++) {
        const ttf_font_item_t* item = ttf_font_item(i);
        if (item != NULL && on_card(item->path)) s_font_n++;
    }
    ensure_reading_font();
    if (!s_card_missing) {
        // 约定的文件夹缺了就建，卡插到电脑上一看就知道往哪放东西。已存在时 mkdir 只是失败，无妨。
        // Create the agreed folders when missing so the card explains itself on a computer. mkdir just fails when they exist.
        (void)mkdir(k_book_dirs[0], 0775);
        (void)mkdir(FONTS_DIR, 0775);
#if READER_SCREENSHOT
        (void)mkdir(SHOTS_DIR, 0775);
#endif
    }
    if (s_books == NULL) s_books = calloc(LIB_MAX, sizeof(*s_books));
    if (s_books == NULL) {
        copy_text(s_notice, sizeof(s_notice), "内存不足，无法列出图书");
        free(old);
        return;
    }
    // 没有一个中英文都全的字体（机身字体丢了，卡上也没有）：还能读，只是会缺字，提醒一句。
    // No font covering Chinese and Latin (built-in font lost, none on the card): still readable with missing characters; say so.
    if (!font_covers_both()) copy_text(s_notice, sizeof(s_notice), "没有可用的字体，中文会缺字");
    for (int d = 0; d < BOOK_DIR_N; d++) {
        if (s_card_missing && on_card(k_book_dirs[d])) continue;
        DIR* dir = opendir(k_book_dirs[d]);
        if (dir == NULL) continue;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && s_book_n < LIB_MAX) {
            if (entry->d_name[0] == '.' || !has_epub_ext(entry->d_name)) continue;
            if (strlen(entry->d_name) >= NAME_BYTES) continue;
            lib_entry_t* item = &s_books[s_book_n];
            memset(item, 0, sizeof(*item));
            item->dir = (uint8_t)d;
            copy_text(item->name, sizeof(item->name), entry->d_name);
            char path[PATH_BYTES];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", k_book_dirs[d], item->name);
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            book_progress_t progress = { 0 };
            if (book_progress_load(path, (uint32_t)st.st_size, &progress)) {
                item->started = true;
                item->pct = progress.pct;
                item->recent = progress.last_open_s;  // 字段名是旧的，存的是阅读先后的序号 / legacy name; holds the reading sequence
            }
            for (int i = 0; i < old_n && old != NULL; i++) {
                if (old[i].meta_ready && old[i].dir == item->dir && strcmp(old[i].name, item->name) == 0) {
                    item->meta_ready = true;
                    memcpy(item->title, old[i].title, sizeof(item->title));
                    memcpy(item->author, old[i].author, sizeof(item->author));
                    memcpy(item->year, old[i].year, sizeof(item->year));
                    break;
                }
            }
            s_book_n++;
        }
        closedir(dir);
    }
    free(old);
    qsort(s_books, (size_t)s_book_n, sizeof(*s_books), compare_entries);
    const int64_t t0 = esp_timer_get_time();
    pages_rebuild(s_lib_page);
    ESP_LOGI(TAG, "library: %d books, card %s, metadata %d ms", s_book_n, s_card_missing ? "absent" : "ready",
             (int)((esp_timer_get_time() - t0) / 1000));
}

// "开始使用"引导页：卡没插或没有书时代替空书库。三步各占一段，右侧写这一步现在的状态。
// The "getting started" page shown instead of an empty library. Three steps, each with its current status on the right.
static void draw_onboarding(uint8_t* fb) {
    ui_clear_page(fb);
    ui_text(fb, UI_MARGIN, 36, UI_PX_TITLE, "开始使用", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 128, UI_MARGIN, ui_content_width(), UI_GRAY_BLACK);

    char fonts[32], books[32];
    snprintf(fonts, sizeof(fonts), "已有 %d 个", s_font_n);
    snprintf(books, sizeof(books), "已有 %d 本", s_book_n);
    const bool card = !s_card_missing;
    const struct {
        const char* title;
        const char* detail;
        const char* status;
    } steps[3] = {
        { "1  插入 TF 卡", "卡要格式化成 FAT32", card ? "已就绪" : s_card_probing ? "正在读取…" : "还没有" },
        { "2  放入字体", "把 TTF 字体放进卡上的 fonts 文件夹", card && s_font_n > 0 ? fonts : "还没有" },
        { "3  放入书", "把 EPUB 电子书放进卡上的 books 文件夹", card && s_book_n > 0 ? books : "还没有" },
    };
    for (int i = 0; i < 3; i++) {
        const int y = 184 + i * 176;
        ui_text(fb, UI_MARGIN, y, UI_PX_LABEL, steps[i].title, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, ui_content_right(), y + 8, UI_PX_CAPTION, steps[i].status, EPD_DRAW_ALIGN_RIGHT, false);
        ui_text(fb, UI_MARGIN, y + 64, UI_PX_CAPTION, steps[i].detail, EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, y + 136, UI_MARGIN, ui_content_width(), UI_GRAY_LIGHT);
    }
    ui_text(fb, UI_MARGIN, 184 + 3 * 176 + 8, UI_PX_CAPTION, "放好后把卡插回设备，书库会自动出现。", EPD_DRAW_ALIGN_LEFT, false);
    if (s_notice[0]) {
        ui_text(fb, UI_MARGIN, 184 + 3 * 176 + 64, UI_PX_CAPTION, s_notice, EPD_DRAW_ALIGN_LEFT, false);
    }
}

static void draw_library_footer(uint8_t* fb);

static void draw_library(uint8_t* fb) {
    if (s_resume_pending) {
        // 开机续读：书还没打开，先说一声，不画书库（免得白刷一遍）。
        // Resuming after boot: the book is not open yet; say so instead of drawing the library (saving a refresh).
        ui_clear_page(fb);
        ui_text(fb, UI_LOCK_WIDTH / 2, UI_LOCK_HEIGHT / 2 - 24, UI_PX_BODY, "正在回到上次读的地方…", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    if (s_card_probing) {
        // 卡正在挂载：等它一下再画书库，免得先画机身的书、马上又重画一遍。
        // The card is mounting: wait for it rather than drawing built-in books and redrawing at once.
        ui_clear_page(fb);
        ui_text(fb, UI_LOCK_WIDTH / 2, UI_LOCK_HEIGHT / 2 - 24, UI_PX_BODY, "正在读取…", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    if (s_book_n == 0) {
        draw_onboarding(fb);
        return;
    }
    ui_clear_page(fb);
    ui_text(fb, UI_MARGIN, 36, UI_PX_TITLE, "书库", EPD_DRAW_ALIGN_LEFT, false);
    if (s_book_n > 0) {
        char count[32];
        snprintf(count, sizeof(count), "%d 本", s_book_n);
        ui_text(fb, ui_content_right(), 66, UI_PX_CAPTION, count, EPD_DRAW_ALIGN_RIGHT, false);
    }
    ui_hairline(fb, 128, UI_MARGIN, ui_content_width(), UI_GRAY_BLACK);
    // 提示（打不开的书、没有字体）写在标题行右侧的数量下面那一行之外：放在标题和分隔线之间的右侧。
    // A notice (a book that failed to open, no fonts) sits to the right of the title, above the rule.
    if (s_notice[0]) {
        char note[128];
        copy_text(note, sizeof(note), s_notice);
        fit_text(note, READ_FOOTER_PX, ui_content_width() - 220);
        ui_text(fb, UI_MARGIN + 150, 70, READ_FOOTER_PX, note, EPD_DRAW_ALIGN_LEFT, false);
    }
    lib_row_t rows[LIB_ROWS];
    const int n = page_rows(rows);
    for (int row = 0; row < n; row++) {
        const lib_entry_t* item = &s_books[rows[row].index];
        const int y = rows[row].y, h = rows[row].h;
        book_meta_t meta;
        entry_meta(item, &meta);
        const int text_w = entry_text_width(item);
        if (item->started) {
            char pct[8];
            snprintf(pct, sizeof(pct), "%u%%", (unsigned)item->pct);
            ui_text_vc(fb, ui_content_right(), y + h / 2, UI_PX_CAPTION, pct, EPD_DRAW_ALIGN_RIGHT, false);
        }
        // 书名放不下一行就折行（最多三行），这一栏跟着变高；作者和年份在书名下面。
        // A title too long for one line wraps (up to three lines) and its row grows; author and year sit below.
        char lines[LIB_TITLE_LINES][NAME_BYTES];
        lines[0][0] = 0;
        const int line_n = wrap_title(meta.title, LIB_PX, text_w, lines);
        const bool has_sub = meta.author[0] || meta.year[0];
        const int first = has_sub ? y + 30 : y + h / 2 - (line_n - 1) * LIB_TITLE_LINE_H / 2;
        for (int k = 0; k < line_n; k++) {
            ui_text_vc(fb, UI_MARGIN, first + k * LIB_TITLE_LINE_H, LIB_PX, lines[k], EPD_DRAW_ALIGN_LEFT, false);
        }
        if (has_sub) {
            char sub[NAME_BYTES + 16];
            snprintf(sub, sizeof(sub), "%s%s%s", meta.author, meta.author[0] && meta.year[0] ? " · " : "", meta.year);
            fit_text(sub, READ_FOOTER_PX, text_w);
            ui_text_vc(fb, UI_MARGIN, y + h - 24, READ_FOOTER_PX, sub, EPD_DRAW_ALIGN_LEFT, false);
        }
        ui_hairline(fb, y + h - 1, UI_MARGIN, ui_content_width(), UI_GRAY_LIGHT);
    }
    draw_library_footer(fb);
}

// 页脚：左下角"上一页"、右下角"下一页"，第一页没有上一页、最后一页没有下一页；中间是"第几页/共几页"。
// 总页数要等所有书的书目信息都读过才知道（书名折行决定每页放几本），没算出来时先写省略号。
// Footer: "previous" in the bottom-left corner and "next" in the bottom-right; the first page has no
// previous and the last no next; "page/total" sits in the middle. The total is known only once every
// book's metadata has been read (wrapped titles decide how many fit per page) and shows an ellipsis until then.
#define LIB_FOOT_Y (UI_LOCK_HEIGHT - 76)
#define LIB_FOOT_H 76
#define LIB_FOOT_TAP_W 228

static EpdRect library_footer_rect(void) { return (EpdRect){ 0, LIB_FOOT_Y, UI_LOCK_WIDTH, LIB_FOOT_H }; }

static void draw_library_footer(uint8_t* fb) {
    if (s_book_n == 0) return;
    const int y = LIB_FOOT_Y + LIB_FOOT_H / 2;
    if (s_lib_page > 0) ui_text_vc(fb, UI_MARGIN, y, UI_PX_CAPTION, "上一页", EPD_DRAW_ALIGN_LEFT, false);
    if (page_has_next()) ui_text_vc(fb, ui_content_right(), y, UI_PX_CAPTION, "下一页", EPD_DRAW_ALIGN_RIGHT, false);
    char middle[24];
    if (s_page_total > 0) snprintf(middle, sizeof(middle), "%d/%d", s_lib_page + 1, s_page_total);
    else snprintf(middle, sizeof(middle), "%d/…", s_lib_page + 1);
    ui_text_vc(fb, UI_LOCK_WIDTH / 2, y, UI_PX_CAPTION, middle, EPD_DRAW_ALIGN_CENTER, false);
}

// 空闲时接着读后面各本书的书目信息，全部读完就把每一页的起点算出来，总页数才有。
// 一次只读两本，免得卡住点按。返回 true 表示总页数刚刚算出来。
// While idle keep reading the metadata of later books; once all are read, lay every page out so the total
// is known. Two books at a time so taps are not held up. Returns true when the total has just become known.
static bool library_idle_step(void) {
    if (s_books == NULL || s_page_total > 0 || s_book_n == 0) return false;
    // 已经读过的直接跳过，不占这一轮的名额。/ Entries already read are passed over without using up this round.
    for (int k = 0; k < 2 && s_fill_index < s_book_n;) {
        if (!s_books[s_fill_index].meta_ready) k++;
        entry_load_meta(&s_books[s_fill_index++]);
    }
    if (s_fill_index < s_book_n) return false;
    const int shown = s_lib_page;
    int total = 0;
    s_lib_page = 0;
    s_page_start[0] = 0;
    for (;;) {
        prepare_page();
        total++;
        if (!page_has_next() || total > LIB_MAX) break;
        s_lib_page++;
    }
    s_lib_page = shown < total ? shown : total - 1;
    s_page_total = total;
    return true;
}

/* ---- 阅读 / reading ---- */

static unsigned percent(size_t page) {
    const uint32_t total = book_total_bytes();
    if (!total) return 0;
    if (book_layout_complete() && s_chapter + 1 == book_chapter_count() && page + 1 == book_layout_page_count()) {
        return 100;
    }
    const uint32_t end = s_chapter + 1 < book_chapter_count() ? book_chapter_byte_offset(s_chapter + 1) : total;
    const uint32_t off = book_position_bytes(
        book_chapter_byte_offset(s_chapter), end, book_layout_page_start_offset(page), s_text_len
    );
    return (unsigned)((uint64_t)off * 100 / total);
}

static void draw_reading(uint8_t* fb) {
    ui_clear_page(fb);
    book_layout_draw_page(fb, s_page, body_rect(), s_px);
    // 页脚：左边章节名，右边"本章页码 · 全书百分比"。排版没跑完时总页数先写省略号。
    // Footer: chapter title on the left, "page in chapter · percent of book" on the right. Total pages show an ellipsis until pagination finishes.
    char right[48], left[160];
    if (book_layout_complete()) {
        snprintf(right, sizeof(right), "%u/%u · %u%%", (unsigned)s_page + 1, (unsigned)book_layout_page_count(), percent(s_page));
    } else {
        snprintf(right, sizeof(right), "%u/… · %u%%", (unsigned)s_page + 1, percent(s_page));
    }
    const int y = UI_LOCK_HEIGHT - READ_FOOTER_H + 26;
    const int right_w = ttf_text_width_px(READ_FOOTER_PX, right);
    copy_text(left, sizeof(left), s_chapter_title);
    fit_text(left, READ_FOOTER_PX, ui_content_width() - right_w - 32);
    ui_text(fb, UI_MARGIN, y, READ_FOOTER_PX, left, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, ui_content_right(), y, READ_FOOTER_PX, right, EPD_DRAW_ALIGN_RIGHT, false);
}

static void save_progress(void) {
    if (s_text == NULL || !s_path[0] || !book_layout_page_count()) return;
    const book_progress_t progress = {
        .file_size = s_file_size,
        .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)book_layout_page_start_offset(s_page),
        .px = (uint8_t)s_px,
        .pct = (uint8_t)percent(s_page),
    };
    const esp_err_t err = book_progress_save(s_path, &progress);
    if (err != ESP_OK) ESP_LOGW(TAG, "progress save failed: %s", esp_err_to_name(err));
    else s_unsaved_turns = 0;
}

static void free_book(void) {
    // 先让锁屏不再指着封面，再释放。/ Point the lock screen away from the cover before freeing it.
    app_lock_set_image(NULL);
    free(s_cover);
    s_cover = NULL;
    s_cover_pending = false;
    book_layout_free();
    free(s_text);
    html_blocks_free(s_blocks, s_block_count);
    s_text = NULL;
    s_text_len = 0;
    s_blocks = NULL;
    s_block_count = 0;
    book_close();
    s_path[0] = 0;
    s_unsaved_turns = 0;
}

// 把一章排到包含 offset 的那一页为止（last_page 时排到章末），其余页之后按需再排。
// Paginate a chapter up to the page holding offset (to the end when last_page); later pages are laid out on demand.
static bool layout_text(const char* utf8, size_t len, const blk_t* blocks, size_t count, size_t offset, bool last_page) {
    // 记下这次排版用的字体，字体换了要重排。/ Remember the font this layout used; a font change needs a relayout.
    copy_text(s_layout_font, sizeof(s_layout_font), ttf_font_path());
    const int64_t layout_start = esp_timer_get_time();
    bool ok = book_layout_begin_blocks(utf8, len, blocks, count, body_rect(), s_px);
    while (ok && !book_layout_complete()
           && (last_page || book_layout_page_start_offset(book_layout_page_count()) <= offset)) {
        ok = book_layout_extend(2);
    }
    ESP_LOGI(TAG, "perf layout: %d ms, pages=%u complete=%d ok=%d",
             (int)((esp_timer_get_time() - layout_start) / 1000),
             (unsigned)book_layout_page_count(), book_layout_complete(), ok);
    return ok;
}

static bool load_chapter(size_t chapter, size_t offset, bool last_page) {
    html_text_t loaded = { 0 };
    const esp_err_t err = book_chapter_load_blocks(chapter, &loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter %u load failed: %s", (unsigned)chapter, esp_err_to_name(err));
        return false;
    }
    if (!layout_text(loaded.utf8, loaded.len, loaded.blocks, loaded.count, offset, last_page)) {
        html_text_free(&loaded);
        return false;
    }
    free(s_text);
    html_blocks_free(s_blocks, s_block_count);
    s_text = loaded.utf8;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    s_chapter = chapter;
    s_page = last_page ? book_layout_page_count() - 1 : book_layout_page_for_offset(offset);
    if (book_chapter_title(chapter, s_chapter_title, sizeof(s_chapter_title)) != ESP_OK || !s_chapter_title[0]) {
        copy_text(s_chapter_title, sizeof(s_chapter_title), s_book_title);
    }
    return true;
}

// 字号或字体变了：当前章按新参数重排，停在原来读到的位置。
// Size or font changed: re-paginate the current chapter and stay at the same reading position.
static bool relayout(void) {
    if (s_view != VIEW_READING || s_text == NULL) return true;
    const size_t offset = book_layout_page_start_offset(s_page);
    if (!layout_text(s_text, s_text_len, s_blocks, s_block_count, offset, false)) return false;
    s_page = book_layout_page_for_offset(offset);
    return true;
}

static void back_to_library(const char* notice) {
    s_resume_pending = false;
    // 回到书库就不再"开机续读"了。/ Back in the library, the next boot no longer resumes a book.
    (void)book_progress_set_last_path("");
    free_book();
    s_view = VIEW_LIBRARY;
    s_menu = false;
    s_lib_page = 0;  // 刚读的书排到了最前面，回到第一页才看得到它 / the book just read is now first; page one shows it
    library_scan();
    if (notice != NULL) copy_text(s_notice, sizeof(s_notice), notice);
}

static bool load_text_chapter(size_t chapter, size_t offset, bool last_page, int dir);

static app_redraw_t open_book(app_ctx_t* ctx, int index, bool feedback) {
    if (index < 0 || index >= s_book_n) return APP_REDRAW_NONE;
    s_work_started_us = esp_timer_get_time();
    s_resume_pending = false;
    char path[PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%s", k_book_dirs[s_books[index].dir], s_books[index].name);
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_notice, sizeof(s_notice), "打不开这本书，请检查 TF 卡");
        return APP_REDRAW_PAGE;
    }
    // 解析大书要一会儿，先给个回应。/ Parsing a large book takes a moment; acknowledge the tap first.
    if (feedback) show_toast(ctx, "正在打开…");
    s_toast_until_ms = 0;

    const esp_err_t err = book_open(path);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
        copy_text(s_notice, sizeof(s_notice), err == ESP_ERR_NO_MEM ? "内存不足，打不开这本书" : "这本书的格式读不了");
        return APP_REDRAW_PAGE;
    }
    copy_text(s_path, sizeof(s_path), path);
    entry_load_meta_from(&s_books[index], true);
    copy_text(s_book_title, sizeof(s_book_title), s_books[index].title);
    s_file_size = (uint32_t)st.st_size;
    s_px = app_settings_book_px();

    book_progress_t progress = { 0 };
    const bool resume = book_progress_load(path, s_file_size, &progress) && progress.chapter < book_chapter_count();
    if (!load_text_chapter(resume ? progress.chapter : 0, resume ? progress.byte_off : 0, false, 1)) {
        back_to_library("排版失败：内存不足或章节过长");
        return APP_REDRAW_PAGE;
    }
    s_view = VIEW_READING;
    s_menu = false;
    s_cover_pending = true;
    (void)book_progress_set_last_path(s_path);
    save_progress();
    ESP_LOGI(TAG, "opened %s chapter=%u page=%u px=%d", s_book_title, (unsigned)s_chapter, (unsigned)s_page + 1, s_px);
    return APP_REDRAW_PAGE;
}

// 当前这一章有没有字。只有图片的章节（封面、插图页）在这个只读文字的阅读器里是一张白页。
// Whether the loaded chapter has any text. A chapter of images only (a cover, a plate) is a blank page in this text-only reader.
static bool chapter_blank(void) {
    if (s_text == NULL) return true;
    for (size_t i = 0; i < s_text_len; i++) {
        const unsigned char c = (unsigned char)s_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        bool in_image = false;
        for (size_t b = 0; b < s_block_count && !in_image; b++) {
            const blk_t* block = &s_blocks[b];
            in_image = (block->image_src != NULL || block->image != NULL) && i >= block->offset && i < block->offset + block->len;
        }
        if (!in_image) return false;
    }
    return true;
}

// 加载一章；它没有字就朝 dir 的方向接着找下一章，免得翻出白页。最多连跳这么多章。
// Load a chapter; when it has no text keep going in direction dir so no blank page turns up. At most this many in a row.
#define BLANK_SKIP_MAX 30
static bool load_text_chapter(size_t chapter, size_t offset, bool last_page, int dir) {
    for (int skipped = 0;; skipped++) {
        if (!load_chapter(chapter, offset, last_page)) return false;
        const bool more = dir > 0 ? chapter + 1 < book_chapter_count() : chapter > 0;
        if (!chapter_blank() || !more || skipped >= BLANK_SKIP_MAX) return true;
        chapter = dir > 0 ? chapter + 1 : chapter - 1;
        offset = 0;
        last_page = dir < 0;
    }
}

static app_redraw_t turn_page(int dir) {
    if (s_text == NULL) return APP_REDRAW_NONE;
    s_work_started_us = esp_timer_get_time();
    if (dir > 0 && s_page + 1 >= book_layout_page_count() && !book_layout_complete()) {
        if (!book_layout_extend(2)) {
            back_to_library("排版失败：内存不足或章节过长");
            return APP_REDRAW_PAGE;
        }
    }
    bool ok = true;
    if (dir > 0 && s_page + 1 < book_layout_page_count()) {
        ++s_page;
    } else if (dir < 0 && s_page > 0) {
        --s_page;
    } else if (dir > 0 && s_chapter + 1 < book_chapter_count()) {
        save_progress();
        ok = load_text_chapter(s_chapter + 1, 0, false, 1);
    } else if (dir < 0 && s_chapter > 0) {
        save_progress();
        ok = load_text_chapter(s_chapter - 1, 0, true, -1);
    } else {
        s_work_started_us = 0;
        return APP_REDRAW_NONE;  // 已经在书的第一页或最后一页 / already at the first or last page of the book
    }
    if (!ok) {
        back_to_library("这一章读不出来");
        return APP_REDRAW_PAGE;
    }
    if (++s_unsaved_turns >= SAVE_EVERY_TURNS) save_progress();
    return APP_REDRAW_PAGE;
}

/* ---- 设置面板 / settings panel ---- */

typedef struct {
    EpdRect panel;
    int size_label_y, font_label_y, refresh_label_y;
#if READER_METALIO
    int light_label_y, warmth_label_y;
    EpdRect light[4], warmth[3];
#endif
    EpdRect size[SIZE_N];
    EpdRect font[FONT_MAX];
    int font_n;
    EpdRect refresh[REFRESH_N];
} menu_geom_t;

static int font_chip_count(void) {
    const int n = ttf_font_count();
    return n > FONT_MAX ? FONT_MAX : n;
}

// 面板里第 i 个字体：机身自带的排在最前，其余按字体模块的顺序。
// The i-th font in the panel: built-in ones first, the rest in the font module's order.
static const ttf_font_item_t* font_chip_item(int i) {
    const int n = ttf_font_count();
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < n; k++) {
            const ttf_font_item_t* item = ttf_font_item(k);
            if (item == NULL || on_flash(item->path) != (pass == 0)) continue;
            if (i-- == 0) return item;
        }
    }
    return NULL;
}

// 面板贴着屏幕底边，高度随字体数量变。画和点共用这一份几何。
// The panel hugs the bottom edge; its height follows the number of fonts. Drawing and hit-testing share this geometry.
static void menu_geometry(menu_geom_t* g) {
    memset(g, 0, sizeof(*g));
    g->font_n = font_chip_count();
    const int font_rows = g->font_n > 0 ? (g->font_n + 1) / 2 : 0;
    const int font_h = font_rows > 0 ? font_rows * CHIP_H + (font_rows - 1) * UI_GAP : LABEL_H;
    const int height =
#if READER_METALIO
        2 * (LABEL_H + CHIP_H + 20) +
#endif
        28 + (LABEL_H + CHIP_H + 20) + (LABEL_H + font_h + 20) + (LABEL_H + CHIP_H) + 36;
    g->panel = (EpdRect){ 0, UI_LOCK_HEIGHT - height, UI_LOCK_WIDTH, height };

    int y = g->panel.y + 28;
    g->size_label_y = y;
    y += LABEL_H;
    for (int i = 0; i < SIZE_N; i++) g->size[i] = ui_grid_rect(i, SIZE_N, 0, y, CHIP_H);
    y += CHIP_H + 20;

    g->font_label_y = y;
    y += LABEL_H;
    for (int i = 0; i < g->font_n; i++) g->font[i] = ui_grid_rect(i % 2, 2, i / 2, y, CHIP_H);
    y += font_h + 20;

    g->refresh_label_y = y;
    y += LABEL_H;
    for (int i = 0; i < REFRESH_N; i++) g->refresh[i] = ui_grid_rect(i, REFRESH_N, 0, y, CHIP_H);
#if READER_METALIO
    y += CHIP_H + 20;
    g->light_label_y = y;
    y += LABEL_H;
    for (int i = 0; i < 4; i++) g->light[i] = ui_grid_rect(i, 4, 0, y, CHIP_H);
    y += CHIP_H + 20;
    g->warmth_label_y = y;
    y += LABEL_H;
    for (int i = 0; i < 3; i++) g->warmth[i] = ui_grid_rect(i, 3, 0, y, CHIP_H);
#endif
}

// 面板里的选项：都有黑色细框，选中的加粗框。不用 ui_draw_chip，是因为它给未选中项画的是
// 浅灰框，而面板用快刷（只有黑白），浅灰框会直接消失。
// A choice in the panel: thin black outline, heavier when selected. ui_draw_chip is avoided because
// its unselected outline is light gray, which vanishes under the panel's fast (black-and-white) update.
static void draw_choice(uint8_t* fb, EpdRect rect, const char* label, bool on) {
    if (on) ui_draw_selected_round_rect(fb, rect, UI_CHIP_RADIUS);
    else ui_draw_round_rect(fb, rect, UI_CHIP_RADIUS, UI_GRAY_BLACK);
    ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2, UI_PX_LABEL_SM, label, EPD_DRAW_ALIGN_CENTER, false);
}

static void draw_menu(uint8_t* fb) {
    menu_geom_t g;
    menu_geometry(&g);
    ui_clear_rect_fast(fb, g.panel);
    epd_fill_rect((EpdRect){ 0, g.panel.y, UI_LOCK_WIDTH, 3 }, UI_GRAY_BLACK, fb);

    ui_text(fb, UI_MARGIN, g.size_label_y, UI_PX_CAPTION, "字号", EPD_DRAW_ALIGN_LEFT, false);
    const int px = app_settings_book_px();
    for (int i = 0; i < SIZE_N; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%u", (unsigned)k_sizes[i]);
        draw_choice(fb, g.size[i], label, k_sizes[i] == px);
    }

    ui_text(fb, UI_MARGIN, g.font_label_y, UI_PX_CAPTION, "字体", EPD_DRAW_ALIGN_LEFT, false);
    if (g.font_n == 0) {
        ui_text(fb, UI_MARGIN + 96, g.font_label_y, UI_PX_CAPTION, "没有找到字体", EPD_DRAW_ALIGN_LEFT, false);
    }
    for (int i = 0; i < g.font_n; i++) {
        const ttf_font_item_t* item = font_chip_item(i);
        if (item == NULL) continue;
        char label[TTF_FONT_NAME_MAX];
        copy_text(label, sizeof(label), on_flash(item->path) ? "内置" : item->name);
        fit_text(label, UI_PX_LABEL_SM, g.font[i].width - 2 * UI_PAD);
        draw_choice(fb, g.font[i], label, !ttf_font_is_builtin() && strcmp(ttf_font_path(), item->path) == 0);
    }

    ui_text(fb, UI_MARGIN, g.refresh_label_y, UI_PX_CAPTION, "刷新", EPD_DRAW_ALIGN_LEFT, false);
    for (int i = 0; i < REFRESH_N; i++) {
        draw_choice(fb, g.refresh[i], k_refresh_names[i], (int)app_settings_reader_refresh() == i);
    }
#if READER_METALIO
    const char* levels[] = {"关", "低", "中", "高"};
    const char* warmth[] = {"冷", "中", "暖"};
    ui_text(fb, UI_MARGIN, g.light_label_y, UI_PX_CAPTION, "前光亮度", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, g.warmth_label_y, UI_PX_CAPTION, "前光色温", EPD_DRAW_ALIGN_LEFT, false);
    for (int i = 0; i < 4; i++) draw_choice(fb, g.light[i], levels[i], app_settings_light_level() == i);
    for (int i = 0; i < 3; i++) draw_choice(fb, g.warmth[i], warmth[i], app_settings_light_warmth() == i);
#endif

}

static app_redraw_t menu_tap(uint16_t x, uint16_t y) {
    menu_geom_t g;
    menu_geometry(&g);
    if (!ui_rect_hit(g.panel, x, y)) {
        s_menu = false;  // 点面板外面就是关掉 / a tap outside closes the panel
        return APP_REDRAW_PAGE;
    }
#if READER_METALIO
    for (int i = 0; i < 4; i++) {
        if (!ui_rect_hit(g.light[i], x, y)) continue;
        app_settings_set_light(i, app_settings_light_warmth());
        reader_board_set_light(app_settings_light_level(), app_settings_light_warmth());
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < 3; i++) {
        if (!ui_rect_hit(g.warmth[i], x, y)) continue;
        app_settings_set_light(app_settings_light_level(), i);
        reader_board_set_light(app_settings_light_level(), app_settings_light_warmth());
        return APP_REDRAW_PAGE;
    }
#endif
    for (int i = 0; i < SIZE_N; i++) {
        if (!ui_rect_hit(g.size[i], x, y) || k_sizes[i] == app_settings_book_px()) continue;
        const int old = s_px;
        s_px = k_sizes[i];
        if (!relayout()) {
            s_px = old;
            if (!relayout()) back_to_library("排版失败：内存不足或章节过长");
            return APP_REDRAW_PAGE;
        }
        app_settings_set_book_px((uint8_t)s_px);
        save_progress();
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < g.font_n; i++) {
        const ttf_font_item_t* item = font_chip_item(i);
        if (item == NULL || !ui_rect_hit(g.font[i], x, y)) continue;
        if (!ttf_font_is_builtin() && strcmp(ttf_font_path(), item->path) == 0) return APP_REDRAW_NONE;
        // item 指向字体模块的列表，换字体前先把路径抄出来。
        // item points into the font module's list; copy the path before switching fonts.
        char path[TTF_FONT_PATH_MAX];
        copy_text(path, sizeof(path), item->path);
        if (ttf_font_open(path) != ESP_OK) {
            ESP_LOGW(TAG, "font open failed: %s", path);
            return APP_REDRAW_PAGE;
        }
        app_settings_set_font_path(path);
        app_settings_set_font_user_picked(true);
        if (!relayout()) back_to_library("排版失败：内存不足或章节过长");
        // 字体变了，书名折在哪里跟着变，书库各页的起点要重排。
        // A new font moves where titles wrap, so the library's page starts are laid out again.
        if (s_view == VIEW_LIBRARY) pages_rebuild(s_lib_page);
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < REFRESH_N; i++) {
        if (!ui_rect_hit(g.refresh[i], x, y)) continue;
        app_settings_set_reader_refresh((app_reader_refresh_t)i);
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

/* ---- 截图（第一版关闭）/ screenshot (off in v1) ---- */
#if READER_SCREENSHOT

static void take_screenshot(app_ctx_t* ctx) {
    if (!card_ready()) {
        show_toast(ctx, "截图失败：没有 TF 卡");
        return;
    }
    (void)mkdir(SHOTS_DIR, 0775);
    // 时间取电源芯片里的实时时钟。/ Time comes from the real-time clock in the PMU.
    time_t now = time(NULL);

    struct tm tm;
    localtime_r(&now, &tm);
    char stem[100], path[PATH_BYTES], name[160];
    copy_text(stem, sizeof(stem), s_view == VIEW_READING ? s_book_title : "书库");
    snprintf(
        name, sizeof(name), "%s_%04d%02d%02d-%02d%02d%02d", stem, tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
    );

    // 注意：写完不要调 read_pico_sd_sync()，它会把卡弹出。fclose 已经把数据交给文件系统了。
    // Note: do not call read_pico_sd_sync() after the write; it ejects the card. fclose already hands the data to the filesystem.
    const esp_err_t err = reader_screenshot_write(SHOTS_DIR, name, ctx->fb, epd_width(), epd_height(), path, sizeof(path)) ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "screenshot %s: %s", path, esp_err_to_name(err));
    char line[200];
    if (err == ESP_OK) snprintf(line, sizeof(line), "已截图 %s", name);
    else snprintf(line, sizeof(line), "截图失败：写不进 TF 卡");
    show_toast(ctx, line);
}

#endif  // READER_SCREENSHOT

/* ---- 页面回调 / page callbacks ---- */

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    if (s_view == VIEW_READING && s_text != NULL) draw_reading(fb);
    else draw_library(fb);
    if (s_menu) draw_menu(fb);
}

// 自己决定用哪种刷新：面板操作用快刷求跟手；翻页按用户选的档；"清除残影"和进页用全刷。
// Choose the refresh here: fast updates for the panel, the user's profile for page turns, a full refresh for "清除残影" and on entry.
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw != APP_REDRAW_PAGE && redraw != APP_REDRAW_FULL) return false;
    if (s_resume_pending && s_view == VIEW_LIBRARY) {
        s_force_full = true;
        return true;
    }
    const int64_t draw_start = esp_timer_get_time();
    ttf_bench_begin();
    render(ctx, ctx->fb);
    ttf_bench_stats_t font_stats;
    ttf_bench_end(&font_stats);
    const int64_t present_start = esp_timer_get_time();
    s_toast_until_ms = 0;
    const app_reader_refresh_t profile = app_settings_reader_refresh();
    enum EpdDrawError result;
    if (redraw == APP_REDRAW_FULL || s_force_full) {
        result = update_display_full(ctx->hl);
    } else if (s_menu) {
        result = update_display_mode(ctx->hl, MODE_DU);
    } else if (s_view == VIEW_READING && profile == APP_READER_REFRESH_FAST) {
        result = update_display_mode(ctx->hl, MODE_DU);
    } else if (s_view == VIEW_READING && profile == APP_READER_REFRESH_CLEAR) {
        result = update_display_full(ctx->hl);
    } else {
        result = update_display_mode(ctx->hl, MODE_GL16);
    }
    s_force_full = false;
    guard_draw_result(ctx->hl, result);
    const int64_t done = esp_timer_get_time();
    ESP_LOGI(TAG, "perf page: draw=%d ms refresh=%d ms action=%d ms glyph_hit=%u miss=%u read=%d ms raster=%d ms",
             (int)((present_start - draw_start) / 1000), (int)((done - present_start) / 1000),
             (int)((done - (s_work_started_us ? s_work_started_us : draw_start)) / 1000),
             (unsigned)font_stats.hits, (unsigned)font_stats.misses,
             (int)(font_stats.read_us / 1000), (int)(font_stats.raster_us / 1000));
    s_work_started_us = 0;
    s_idle_after_ms = done / 1000 + 150;
    return true;
}

// 首帧前尝试续读；卡未就绪时仍由 tick 重试，失败正常退回书库。
// Try resuming before the first frame; ticks retry a late card and failures fall back to the library.
static app_redraw_t resume_book(app_ctx_t* ctx) {
    if (s_resume_pending && s_view == VIEW_LIBRARY) {
        if (s_card_missing && card_ready()) library_scan();
        char last[PATH_BYTES], path[PATH_BYTES];
        const bool have_last = book_progress_last_path(last, sizeof(last)) && last[0] != 0;
        for (int i = 0; have_last && i < s_book_n; i++) {
            snprintf(path, sizeof(path), "%s/%s", k_book_dirs[s_books[i].dir], s_books[i].name);
            if (strcmp(path, last) != 0) continue;
            s_resume_pending = false;
            return open_book(ctx, i, false);
        }
        // 没找到：书在卡上而卡还没好就再等等；机身的书不用等卡。等不到就停在书库。
        // Not found: a card book waits for the card; a built-in book does not. Otherwise stay in the library.
        const bool waiting = have_last && on_card(last) && s_card_missing && (card_probing() || ctx->now_ms <= 6000);
        if (waiting) return APP_REDRAW_NONE;
        s_resume_pending = false;
        pages_rebuild(0);
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    // 这一版不显示书里的图片，占位框也不留。/ This version shows no book images and leaves no placeholder.
    book_layout_set_image_placeholders(false);
    // 从锁屏回来时书还开着，就接着读。/ Returning from the lock screen with a book still open: keep reading.
    if (s_view == VIEW_READING && s_text != NULL) return;
    if (!s_booted) {
        // 刚开机：上次锁屏时有书开着（记着它的路径），就准备回到那本书。
        // Fresh boot: a book was open at the last lock (its path is remembered), so plan to reopen it.
        s_booted = true;
        char last[PATH_BYTES];
        s_resume_pending = book_progress_last_path(last, sizeof(last)) && last[0] != 0;
    }
    s_view = VIEW_LIBRARY;
    s_menu = false;
    library_scan();
    (void)resume_book(ctx);
}

// 不能叫 on_exit：标准库 stdlib.h 里已经有一个同名函数。
// Not named on_exit: stdlib.h already declares a function by that name.
static void reader_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    book_layout_set_image_placeholders(true);  // 还给官方的图书演示页 / hand back to the vendor book demo page
    save_progress();
}

static void on_media_lost(app_ctx_t* ctx) {
    (void)ctx;
    s_menu = false;
    s_font_checked = false;  // 卡变了，下次重新找可用字体 / the card changed; search for a usable font again next time
    if (s_view == VIEW_READING && s_text != NULL && on_flash(s_path)) {
        // 正在读机身里的书：书不受影响。字体若在卡上，换成机身字体并重排，接着读。
        // Reading a built-in book: the book is unaffected. A card font is swapped for the built-in one and the page relaid.
        if (ttf_font_on_card()) {
            (void)ttf_font_open_fallback();
            if (!relayout()) back_to_library("排版失败：内存不足或章节过长");
        }
        return;
    }
    // 卡上的书读不了了：不再碰卡上的文件，回书库（里面还有机身的书）。
    // A card book can no longer be read: touch no card files and fall back to the library, which still has the built-in books.
    free_book();
    s_view = VIEW_LIBRARY;
    if (ttf_font_on_card()) (void)ttf_font_open_fallback();
    library_scan();
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    // 等卡时书库还未显示，不能命中它的隐藏行。/ While waiting for the card, never hit-test hidden library rows.
    if (s_resume_pending) return APP_REDRAW_NONE;
    if (s_menu) return ev->type == UI_GESTURE_TAP ? menu_tap(ev->x0, ev->y0) : APP_REDRAW_NONE;
    if (s_view == VIEW_READING) {
        switch (ev->type) {
            case UI_GESTURE_TAP: return turn_page(ev->x0 < READ_PREV_W ? -1 : 1);
            case UI_GESTURE_SWIPE_L: return turn_page(1);
            case UI_GESTURE_SWIPE_R: return turn_page(-1);
            default: return APP_REDRAW_NONE;
        }
    }
    switch (ev->type) {
        case UI_GESTURE_TAP: {
            // 左下角、右下角是上一页、下一页。/ The bottom corners are previous and next.
            if (ev->y0 >= LIB_FOOT_Y) {
                if (ev->x0 < LIB_FOOT_TAP_W && s_lib_page > 0) {
                    s_lib_page--;
                    prepare_page();
                    return APP_REDRAW_PAGE;
                }
                if (ev->x0 >= UI_LOCK_WIDTH - LIB_FOOT_TAP_W && page_has_next()) {
                    s_lib_page++;
                    prepare_page();
                    return APP_REDRAW_PAGE;
                }
                return APP_REDRAW_NONE;
            }
            lib_row_t rows[LIB_ROWS];
            const int n = page_rows(rows);
            for (int row = 0; row < n; row++) {
                if (ev->y0 >= rows[row].y && ev->y0 < rows[row].y + rows[row].h) return open_book(ctx, rows[row].index, true);
            }
            return APP_REDRAW_NONE;
        }
        case UI_GESTURE_SWIPE_U:
        case UI_GESTURE_SWIPE_L:
            if (!page_has_next()) return APP_REDRAW_NONE;
            s_lib_page++;
            prepare_page();
            return APP_REDRAW_PAGE;
        case UI_GESTURE_SWIPE_D:
        case UI_GESTURE_SWIPE_R:
            if (s_lib_page == 0) return APP_REDRAW_NONE;
            s_lib_page--;
            prepare_page();
            return APP_REDRAW_PAGE;
        default:
            return APP_REDRAW_NONE;
    }
}

static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    s_mid_long_armed = false;
#if READER_METALIO
    // Metalio 左键无条件回书库，中键开关设置。/ Metalio left always returns home; middle toggles settings.
    if (key == UI_KEY_1) {
        s_resume_pending = false;
        (void)book_progress_set_last_path("");
        s_menu = false;
        save_progress();
        back_to_library(NULL);
        // 没有卡检测脚，HOME 为插卡后的显式重试入口。/ Without a card-detect pin, HOME explicitly rearms recovery after insertion.
        if (s_card_missing) {
            s_card_retry_count = 0;
            s_card_retry_after_ms = 0;
        }
        return APP_REDRAW_PAGE;
    }
    if (key == READER_INPUT_SCREENSHOT) {
        take_screenshot(ctx);
        return APP_REDRAW_NONE;
    }
    if (key == READER_INPUT_PREVIOUS || key == READER_INPUT_NEXT) {
        if (s_resume_pending || s_menu || s_view != VIEW_READING) return APP_REDRAW_NONE;
        return turn_page(key == READER_INPUT_PREVIOUS ? -1 : 1);
    }
    if (key == UI_KEY_2) key = UI_KEY_1;
#endif
    if (s_resume_pending) {
        if (key != UI_KEY_2) return APP_REDRAW_NONE;
        s_resume_pending = false;
        (void)book_progress_set_last_path("");
        pages_rebuild(0);
        return APP_REDRAW_PAGE;
    }
    switch (key) {
        case UI_KEY_1:  // 左：开关设置面板 / left: toggle the settings panel
            s_menu = !s_menu;
            if (s_menu) (void)ttf_font_scan();
            return APP_REDRAW_PAGE;
        case UI_KEY_2:  // 中：返回上一级 / middle: back one level
            if (s_menu) {
                s_menu = false;
                return APP_REDRAW_PAGE;
            }
            if (s_view == VIEW_READING) {
                save_progress();
                back_to_library(NULL);
                return APP_REDRAW_PAGE;
            }
            s_mid_long_armed = true;
            return APP_REDRAW_NONE;
        case UI_KEY_3:  // 右：全刷一次，清掉残影 / right: one full refresh to clear ghosting
            s_force_full = true;
            return APP_REDRAW_PAGE;
        default:
            return APP_REDRAW_NONE;
    }
}

// 书库页长按中键进硬件演示菜单，给开发和排查用。只认"这次按下时就在书库"，
// 免得从书里返回时按久了一点就误入。
// Long-press the middle key in the library for the hardware demo menu, for development and
// diagnostics. Only a press that began in the library counts, so a slightly long "back" out of
// a book does not trip it.
static app_redraw_t on_key_long(app_ctx_t* ctx, int key) {
#if !READER_METALIO
    if (key == UI_KEY_2 && s_mid_long_armed && s_view == VIEW_LIBRARY && !s_menu) ctx->request_menu = true;
#endif
    s_mid_long_armed = false;
    return APP_REDRAW_NONE;
}

// 只在书库空闲时恢复卡，先关闭全部消费者；失败最多重试三次，避免反复卸载。
// Recover only in an idle library after closing consumers; cap failures at three attempts.
static app_redraw_t recover_library_card(app_ctx_t* ctx) {
    if (s_view != VIEW_LIBRARY || s_menu || ctx->pressed ||
        (ctx->touch != NULL && ctx->touch->touched) || !s_card_missing ||
        ctx->now_ms - s_card_poll_ms < 1000) return APP_REDRAW_NONE;
    s_card_poll_ms = ctx->now_ms;
    reader_media_info_t sd = {0};
    const esp_err_t status = reader_media_get_info(&sd);
    const bool probing = status == ESP_ERR_NOT_FINISHED;
    if (status == ESP_OK && sd.present && sd.mounted) {
        s_font_checked = false;
        library_scan();
        return APP_REDRAW_PAGE;
    }
    if (!sd.present) {
        s_card_retry_count = 0;
        s_card_retry_after_ms = 0;
    } else if (!probing && s_card_retry_count < 3 && ctx->now_ms >= s_card_retry_after_ms) {
        ++s_card_retry_count;
        s_card_retry_after_ms = ctx->now_ms + (1000LL << s_card_retry_count);
        free_book();
        if (ttf_font_on_card() && ttf_font_open_fallback() != ESP_OK) {
            ESP_LOGW(TAG, "SD recovery: font fallback failed");
            return APP_REDRAW_NONE;
        }
        s_font_checked = false;
        const esp_err_t err = reader_media_remount();
        ESP_LOGI(TAG, "SD recovery attempt=%u previous=%s result=%s", s_card_retry_count,
                 esp_err_to_name(status == ESP_OK ? sd.error : status), esp_err_to_name(err));
        library_scan();
        return APP_REDRAW_PAGE;
    }
    if (probing != s_card_probing) {
        library_scan();
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    // 提示条到点后把底下的画面还原，只推那一条，用 GL16 保住文字的灰阶。
    // When the notice expires restore what was under it, pushing only that strip in GL16 to keep text grays.
    if (s_toast_until_ms != 0 && ctx->now_ms >= s_toast_until_ms) {
        s_toast_until_ms = 0;
        render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &READER_WAVEFORM, MODE_GL16, toast_rect()));
        return APP_REDRAW_DONE;
    }
    const app_redraw_t recovered = recover_library_card(ctx);
    if (recovered != APP_REDRAW_NONE) return recovered;
    if (s_resume_pending) {
        const app_redraw_t resumed = resume_book(ctx);
        if (resumed != APP_REDRAW_NONE) return resumed;
    }
    // 字体在排版之后换了（卡上的字体晚一步才加载好，或者退回了机身字体）：按新字体重排当前页。
    // The font changed after layout (a card font finished loading late, or fell back to the built-in one): relay the page.
    if (s_view == VIEW_READING && s_text != NULL && strcmp(s_layout_font, ttf_font_path()) != 0) {
        if (!relayout()) back_to_library("排版失败：内存不足或章节过长");
        return APP_REDRAW_PAGE;
    }
    // 书库空闲时接着读后面的书目信息；总页数一算出来就把页脚那一条重画一下（只推这一条，用 GL16 保住文字的灰阶）。
    // While the library idles keep reading later metadata; once the total is known repaint just the footer strip (GL16 to keep text grays).
    if (s_view == VIEW_LIBRARY && !s_menu && !s_resume_pending && !ctx->pressed && ctx->now_ms >= s_idle_after_ms && s_toast_until_ms == 0 && library_idle_step()) {
        render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &READER_WAVEFORM, MODE_GL16, library_footer_rect()));
        return APP_REDRAW_DONE;
    }
    // 书打开后空闲时把封面图读进来（整理过的书里才有），锁屏时显示它。放在空闲时读，不拖慢开书。
    // Once a book is open, read its cover while idle (only prepared books carry one) for the lock screen. Reading it while idle keeps opening fast.
    if (s_view == VIEW_READING && s_cover_pending && !s_menu && !ctx->pressed && ctx->now_ms >= s_idle_after_ms) {
        s_cover_pending = false;
        if (book_read_file(BOOK_EPUB_COVER_PATH, BOOK_EPUB_COVER_BYTES, &s_cover) == ESP_OK) {
            app_lock_set_image(s_cover);
            ESP_LOGI(TAG, "lock cover ready");
        }
        return APP_REDRAW_NONE;
    }
    // 空闲时接着把本章剩下的页排完，页脚的总页数才出得来。
    // While idle keep paginating the rest of the chapter so the footer can show its page total.
    if (s_view == VIEW_READING && s_text != NULL && !s_menu && !ctx->pressed && ctx->now_ms >= s_idle_after_ms && !book_layout_complete()) {
        if (!book_layout_extend(1)) {
            back_to_library("排版失败：内存不足或章节过长");
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}

const app_desc_t app_reader = {
    .title = "阅读器 Reader",
    .detail = "书库与 EPUB 阅读 Library & EPUB",
    .enter_full = true,
    .owns_keys = true,
    .no_menu = true,
    .on_enter = on_enter,
    .on_exit = reader_on_exit,
    .on_media_lost = on_media_lost,
    .render = render,
    .present = present,
    .on_gesture = on_gesture,
    .on_key = on_key,
    .on_key_long = on_key_long,
    .on_tick = on_tick,
};
