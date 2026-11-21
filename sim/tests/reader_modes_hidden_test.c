/* SPDX-License-Identifier: Apache-2.0
 * 真实阅读器验证模式入口和隐藏书库。/ Exercise real reader modes and hidden-library navigation.
 */
#include "../../main/apps/app_reader.c"
#include "read_pico_board.h"
#include "board/reader_screenshot.h"
#include <assert.h>
static EpdRect first_glyph;
static bool first_cell(const book_layout_glyph_t *g,void *unused){(void)unused;first_glyph=g->rect;return false;}
static void capture(app_ctx_t *ctx,const char *name){const char *dir=getenv("MODES_PREVIEW");if(!dir)return;char path[512];render(ctx,ctx->fb);assert(reader_screenshot_write(dir,name,ctx->fb,1216,684,path,sizeof(path)));}
int main(void){
 e0470_waveform_init();epd_init(&epd_board_read_pico,&E0470_DISPLAY,EPD_LUT_1K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
 app_settings_init();assert(ttf_font_init()==ESP_OK);assert(ttf_font_open("/flash/fonts/Fallback.ttf")==ESP_OK);
 EpdiyHighlevelState hl=epd_hl_init(&READER_WAVEFORM);app_ctx_t ctx={.hl=&hl,.fb=epd_hl_get_framebuffer(&hl),.now_ms=10000};assert(read_pico_sd_remount()==ESP_OK);
 on_enter(&ctx);assert(s_view==VIEW_LIBRARY && s_book_n==2);
 assert(open_book(&ctx,0,false)==APP_REDRAW_PAGE);render(&ctx,ctx.fb);
 (void)book_layout_visit_page(0,body_rect(),s_px,first_cell,NULL);assert(first_glyph.width);
 ui_gesture_event_t hold={.type=UI_GESTURE_LONG_PRESS,.x0=first_glyph.x+first_glyph.width/2,.y0=first_glyph.y+first_glyph.height/2};
 assert(on_gesture(&ctx,&hold)==APP_REDRAW_PAGE && reader_highlight_ui_active() && !reader_dict_ui_active());
 capture(&ctx,"default-hold-highlight");
 assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && !reader_highlight_ui_active());
 assert(!s_lookup_mode);capture(&ctx,"mode-highlight-footer");
 assert(on_key_long(&ctx,UI_KEY_3)==APP_REDRAW_PAGE && s_lookup_mode);capture(&ctx,"mode-dictionary-footer");
 assert(on_gesture(&ctx,&hold)==APP_REDRAW_AREA && reader_dict_ui_holding() && !reader_highlight_ui_active());
 assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && !reader_dict_ui_active());
 reader_on_lock(&ctx);assert(!s_lookup_mode && s_view==VIEW_READING && s_text);
 assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_view==VIEW_LIBRARY);
 // 长按条目仅出操作，抬手不得打开书；隐藏保留实体文件与阅读进度。
 // Holding a row opens actions only; release never opens the book, hiding preserves its file and progress.
 assert(on_key_long(&ctx,UI_KEY_3)==APP_REDRAW_PAGE && reader_vocabulary_ui_active());
 ui_gesture_event_t covered_header={.type=UI_GESTURE_TAP,.x0=90,.y0=80};
 for(int i=0;i<3;i++){ctx.now_ms+=150;(void)on_gesture(&ctx,&covered_header);}assert(!s_hidden_library);
 (void)on_key(&ctx,UI_KEY_2);assert(!reader_vocabulary_ui_active());
 lib_row_t rows[LIB_ROWS];assert(page_rows(rows)==2);lib_entry_t *entry=&s_books[rows[0].index];
 char hidden_path[PATH_BYTES];snprintf(hidden_path,sizeof(hidden_path),"%s/%s",k_book_dirs[entry->dir],entry->name);
 ui_gesture_event_t rowhold={.type=UI_GESTURE_LONG_PRESS,.x0=100,.y0=rows[0].y+30};
 assert(on_gesture(&ctx,&rowhold)==APP_REDRAW_PAGE && s_library_action && s_view==VIEW_LIBRARY);
 rowhold.type=UI_GESTURE_LONG_RELEASE;assert(on_gesture(&ctx,&rowhold)==APP_REDRAW_NONE && s_library_action);
 capture(&ctx,"hide-book-action");
 assert(on_key_long(&ctx,UI_KEY_3)==APP_REDRAW_NONE && !reader_vocabulary_ui_active());
 ui_gesture_event_t confirm={.type=UI_GESTURE_TAP,.x0=480,.y0=600};
 assert(on_gesture(&ctx,&confirm)==APP_REDRAW_PAGE && !s_library_action && s_book_n==1);
 bool hidden=false;assert(book_visibility_get(hidden_path,&hidden)==ESP_OK && hidden);
 struct stat st;assert(stat(hidden_path,&st)==0);
 ui_gesture_event_t header={.type=UI_GESTURE_TAP,.x0=90,.y0=80};
 for(int i=0;i<2;i++){ctx.now_ms+=200;assert(on_gesture(&ctx,&header)==APP_REDRAW_NONE && !s_hidden_library);}
 ctx.now_ms+=200;assert(on_gesture(&ctx,&header)==APP_REDRAW_PAGE && s_hidden_library && s_book_n==1);
 capture(&ctx,"hidden-library");assert(open_book(&ctx,0,false)==APP_REDRAW_PAGE && s_open_hidden);
 assert(!s_cover_pending && !s_cover && !app_lock_has_image());
 char last[PATH_BYTES];assert(!book_progress_last_path(last,sizeof(last)) && !last[0]);
 ctx.now_ms+=5000;(void)on_tick(&ctx);assert(!s_cover && !app_lock_has_image());
 assert(app_reader.on_lock);app_reader.on_lock(&ctx);
 assert(!s_hidden_library && !s_open_hidden && !s_text && s_view==VIEW_LIBRARY && s_book_n==1 && !app_lock_has_image());
 // 浅睡回调后与冷启动都不得恢复隐藏书，即使 last 残留了旧路径。
 // Neither warm wake nor cold resume may reopen hidden books, even with a stale last-path record.
 on_enter(&ctx);assert(s_view==VIEW_LIBRARY && s_book_n==1);
 assert(book_progress_set_last_path(hidden_path)==ESP_OK);s_booted=false;on_enter(&ctx);
 assert(!s_resume_pending && s_view==VIEW_LIBRARY && !s_text && !s_hidden_library);
 assert(!book_progress_last_path(last,sizeof(last)) && !last[0]);
 // 超时或夹杂其他触摸不能累计三击。/ Slow or interrupted taps never accumulate into a triple tap.
 for(int i=0;i<3;i++){ctx.now_ms+=600;(void)on_gesture(&ctx,&header);assert(!s_hidden_library);}
 ui_gesture_event_t elsewhere={.type=UI_GESTURE_TAP,.x0=500,.y0=100};(void)on_gesture(&ctx,&elsewhere);
 for(int i=0;i<3;i++){ctx.now_ms+=180;(void)on_gesture(&ctx,&header);}assert(s_hidden_library);
 // 隐藏列表长按可恢复；空隐藏列表仍可三击返回。
 // Holds can restore a hidden book; the empty hidden list still accepts triple-tap return.
 assert(page_rows(rows)==1);rowhold.type=UI_GESTURE_LONG_PRESS;rowhold.y0=rows[0].y+30;
 assert(on_gesture(&ctx,&rowhold)==APP_REDRAW_PAGE && s_library_action);
 assert(on_gesture(&ctx,&confirm)==APP_REDRAW_PAGE && !s_library_action && s_book_n==0 && s_hidden_library);
 assert(book_visibility_get(hidden_path,&hidden)==ESP_OK && !hidden);capture(&ctx,"empty-hidden-library");
 for(int i=0;i<3;i++){ctx.now_ms+=180;(void)on_gesture(&ctx,&header);}assert(!s_hidden_library && s_book_n==2);
 // 所有书隐藏时，正常书库也必须保留标题入口。/ Even hiding every book must retain the normal library title entry.
 for(int i=0;i<s_book_n;i++){char path[PATH_BYTES];snprintf(path,sizeof(path),"%s/%s",k_book_dirs[s_books[i].dir],s_books[i].name);assert(book_visibility_set(path,true)==ESP_OK);}
 library_scan();assert(!s_book_n);capture(&ctx,"empty-normal-library");
 for(int i=0;i<3;i++){ctx.now_ms+=180;(void)on_gesture(&ctx,&header);}assert(s_hidden_library && s_book_n==2);
 app_reader.on_lock(&ctx);assert(!s_hidden_library && !s_book_n);
 free_book();puts("reader modes and hidden library: PASS");return 0;
}
