/* SPDX-License-Identifier: Apache-2.0
 * 真实阅读页的目录状态、返回和低电量显示。/ Real reader TOC state, back navigation and low-charge presentation.
 */
#define TAG display_test_tag
#include "../../main/display.c"
#undef TAG
static enum EpdDrawMode test_reader_refresh_mode;
static bool test_reader_refresh_area;
static EpdRect test_reader_refresh_rect;
static unsigned test_reader_refresh_calls;
static enum EpdDrawError test_reader_update_mode(EpdiyHighlevelState *hl,enum EpdDrawMode mode) {
    test_reader_refresh_mode=mode;test_reader_refresh_area=false;++test_reader_refresh_calls;
    return update_display_mode(hl,mode);
}
static enum EpdDrawError test_reader_update_area(EpdiyHighlevelState *hl,const EpdWaveform *wave,enum EpdDrawMode mode,EpdRect rect) {
    test_reader_refresh_mode=mode;test_reader_refresh_area=true;test_reader_refresh_rect=rect;++test_reader_refresh_calls;
    return update_display_area_with(hl,wave,mode,rect);
}
#define update_display_mode test_reader_update_mode
#define update_display_area_with test_reader_update_area
#define read_pico_pmu_get test_pmu_get
#include "../../main/apps/app_reader.c"
#undef update_display_mode
#undef update_display_area_with
static size_t dict_alloc_remaining=SIZE_MAX;
static void *dict_test_malloc(size_t bytes) {
    if(dict_alloc_remaining==0)return NULL;
    if(dict_alloc_remaining!=SIZE_MAX)--dict_alloc_remaining;
    return malloc(bytes);
}
#include "../../main/dictionary/reader_dictionary.h"
static unsigned dict_lookup_calls;
static const uint8_t *dict_pending_frame;
static uint8_t *dict_pending_fb;
static size_t dict_pending_bytes;
static reader_dict_status_t dict_test_lookup(const char *query,reader_dict_entry_t *entry) {
    if(dict_pending_frame && memcmp(dict_pending_frame,dict_pending_fb,dict_pending_bytes))abort();
    ++dict_lookup_calls;return reader_dictionary_lookup(query,entry);
}
static reader_dict_status_t dict_test_session_lookup(reader_dict_session_t *session,const char *query,reader_dict_entry_t *entry) {
    if(dict_pending_frame && memcmp(dict_pending_frame,dict_pending_fb,dict_pending_bytes))abort();
    ++dict_lookup_calls;return reader_dictionary_session_lookup(session,query,entry);
}
static bool dict_capture,dict_card_clearance;
static int dict_card_text_limit;
static char dict_draw_text[8192];
static int dict_draw_max_px;
static bool notebook_word_size,notebook_meaning_size;
static void dict_test_ui_text(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverted) {
    if(dict_capture) {
        strlcat(dict_draw_text,text,sizeof(dict_draw_text));strlcat(dict_draw_text,"\n",sizeof(dict_draw_text));
        if(px>dict_draw_max_px)dict_draw_max_px=px;
        if(px==36 && !strcmp(text,"心"))notebook_word_size=true;
        if(px==32 && strstr(text,"心脏；心思"))notebook_meaning_size=true;
        if(dict_card_clearance && px==30 && x+ttf_text_width_px(px,text)>dict_card_text_limit)abort();
    }
    ui_text(fb,x,y,px,text,align,inverted);
}
static bool dict_synthetic_geometry;
static book_layout_glyph_t dict_geometry[32];
static size_t dict_geometry_count;
static bool dict_test_visit_page(size_t page,EpdRect body,int px,book_layout_glyph_visitor_t visitor,void *context) {
    if(!dict_synthetic_geometry)return book_layout_visit_page(page,body,px,visitor,context);
    for(size_t i=0;i<dict_geometry_count;i++)if(!visitor(&dict_geometry[i],context))return false;
    return true;
}
static unsigned dict_measure_calls;
static int dict_test_measure(int px,const char *text) {
    ++dict_measure_calls;return ttf_text_width_px(px,text);
}
#define ttf_text_width_px dict_test_measure
#define ui_text dict_test_ui_text
#define reader_dictionary_lookup dict_test_lookup
#define reader_dictionary_session_lookup dict_test_session_lookup
#define book_layout_visit_page dict_test_visit_page
#define malloc dict_test_malloc
#include "../../main/dictionary/reader_dictionary_ui.c"
#undef ttf_text_width_px
#undef book_layout_visit_page
#undef malloc
#undef reader_dictionary_lookup
#undef reader_dictionary_session_lookup
#undef ui_text
#include "board/reader_usb.h"
static unsigned notebook_opens,notebook_closes;
static bool notebook_session_live,notebook_fail_close;
static reader_dict_status_t notebook_test_open(reader_dict_session_t *session,const char *path) {
    if(notebook_session_live || reader_usb_active())abort();
    ++notebook_opens;notebook_session_live=true;
    return reader_dictionary_session_open(session,path);
}
static reader_dict_status_t notebook_test_close(reader_dict_session_t *session) {
    if(!notebook_session_live)abort();
    ++notebook_closes;notebook_session_live=false;
    reader_dict_status_t status=reader_dictionary_session_close(session);
    return notebook_fail_close?READER_DICT_IO_ERROR:status;
}
#define reader_dictionary_session_open notebook_test_open
#define reader_dictionary_session_close notebook_test_close
#define ui_text dict_test_ui_text
#define reader_dictionary_lookup dict_test_lookup
#define reader_dictionary_session_lookup dict_test_session_lookup
#define malloc dict_test_malloc
#include "../../main/dictionary/reader_vocabulary_ui.c"
#undef reader_dictionary_session_open
#undef reader_dictionary_session_close
#undef malloc
#undef reader_dictionary_lookup
#undef reader_dictionary_session_lookup
#undef ui_text
#include "board/reader_screenshot.h"
#include "read_pico_board.h"
#include <assert.h>
#include <setjmp.h>
static jmp_buf usb_restart_done;
static void test_usb_restart(void) __attribute__((noreturn));
#define esp_restart test_usb_restart
#define reader_usb_start test_usb_start
#define reader_usb_stop test_usb_stop
#define reader_usb_stop_confirmed test_usb_stop_confirmed
#define reader_usb_state test_usb_state
#define reader_usb_active test_usb_active
#define reader_usb_requires_restart test_usb_requires_restart
#define read_pico_sd_start_probe test_usb_probe
#define read_pico_sd_get_info test_usb_media_info
static esp_err_t test_usb_probe(void);
static esp_err_t test_usb_media_info(reader_media_info_t *info);
static bool test_usb_requires_restart(void);
esp_err_t test_usb_start(void);
esp_err_t test_usb_stop(void);
esp_err_t test_usb_stop_confirmed(void);
reader_usb_state_t test_usb_state(void);
bool test_usb_active(void);
#include "../../main/apps/app_usb_transfer.c"
#undef reader_usb_start
#undef reader_usb_stop
#undef reader_usb_stop_confirmed
#undef reader_usb_state
#undef reader_usb_active
#undef reader_usb_requires_restart
#undef read_pico_sd_start_probe
#undef read_pico_sd_get_info
#undef esp_restart
static bool fake_usb_active;
static reader_usb_state_t fake_usb_state;
static esp_err_t fake_usb_start_error, fake_usb_stop_error;
static int confirmed_stops, plain_stops, usb_reboots,usb_probes;
static bool usb_fake_restart,usb_fake_probing,usb_fake_missing,usb_fake_probe_started;
static bool test_usb_requires_restart(void){return usb_fake_restart;}
static esp_err_t test_usb_probe(void){
    assert(!fake_usb_active);
    if(!usb_fake_probe_started){usb_probes++;usb_fake_probing=true;usb_fake_probe_started=true;}
    return usb_fake_probing?ESP_ERR_NOT_FINISHED:usb_fake_missing?ESP_ERR_NOT_FOUND:ESP_OK;
}
static esp_err_t test_usb_media_info(reader_media_info_t *info){
    assert(!fake_usb_active);
    *info=(reader_media_info_t){.present=!usb_fake_missing,.mounted=!usb_fake_missing && !usb_fake_probing};
    return usb_fake_probing?ESP_ERR_NOT_FINISHED:usb_fake_missing?ESP_ERR_NOT_FOUND:ESP_OK;
}
esp_err_t test_usb_start(void){usb_fake_probe_started=false;fake_usb_active=fake_usb_start_error==ESP_OK;fake_usb_state=READER_USB_WAITING;return fake_usb_start_error;}
esp_err_t test_usb_stop(void){++plain_stops;if(fake_usb_state==READER_USB_CONNECTED)return ESP_ERR_INVALID_STATE;if(fake_usb_stop_error)return fake_usb_stop_error;fake_usb_active=false;return ESP_OK;}
esp_err_t test_usb_stop_confirmed(void){++confirmed_stops;if(fake_usb_stop_error)return fake_usb_stop_error;fake_usb_active=false;return ESP_OK;}
reader_usb_state_t test_usb_state(void){return fake_usb_state;}
bool test_usb_active(void){return fake_usb_active;}
static void test_usb_restart(void){++usb_reboots;longjmp(usb_restart_done,1);}
static pmu_snapshot_t battery;
static unsigned battery_reads;
const pmu_snapshot_t* test_pmu_get(void) { ++battery_reads; return &battery; }
static void preview(app_ctx_t* ctx,const char* name) {
    const char* directory=getenv("READER_TOC_PREVIEW");if(!directory)return;
    char path[512];render(ctx,ctx->fb);
    assert(reader_screenshot_write(directory,name,ctx->fb,1216,684,path,sizeof(path)));
}
static void dict_preview(app_ctx_t *ctx,const char *name) {
    const char *directory=getenv("READER_DICT_PREVIEW");
    if(!directory)return;
    char path[512];render(ctx,ctx->fb);
    assert(reader_screenshot_write(directory,name,ctx->fb,1216,684,path,sizeof(path)));
}
static void choose_cell(size_t row) {
    assert(row<d.n);EpdRect r=d.cells[row].rect;
    assert(reader_dict_ui_tap(r.x+r.width/2,r.y+r.height/2));
}
static void choose_units(size_t first,size_t last) {
    assert(first<=last && last<d.n);
    for(size_t i=first;i<=last;i++) {
        if(!d.cells[i].unboxed && (i==first || d.cells[i-1].first!=d.cells[i].first))choose_cell(i);
    }
}
static void reselect_fixture(void) {
    EpdRect icon=reselect_rect();
    assert(d.card && reader_dict_ui_tap(icon.x+1,icon.y+1));
    assert(reader_dict_ui_selecting() && !d.selected_count);
}
static size_t cell_for(const char *text) {
    size_t n=strlen(text);
    for(size_t i=0;i<d.n;i++) {
        cell_t c=d.cells[i];
        if(c.first+n>d.end || memcmp(d.text+c.first,text,n))continue;
        if(latin_base(text,step(text,n)) && c.end-c.first!=n)continue;
        return i;
    }
    assert(false);return 0;
}
static void assert_overlay_pixels(app_ctx_t *ctx,const uint8_t *baseline) {
    size_t bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *mask=malloc(bytes);assert(mask);memset(mask,0xff,bytes);
    epd_fill_rect((EpdRect){0,d.footer,UI_LOCK_WIDTH,UI_LOCK_HEIGHT-d.footer},UI_GRAY_BLACK,mask);
    draw_selection_boxes(mask);
    render(ctx,ctx->fb);
    for(size_t i=0;i<bytes;i++)assert((baseline[i]&mask[i])==(ctx->fb[i]&mask[i]));
    free(mask);
}
static void assert_card_pixels(app_ctx_t *ctx,const uint8_t *baseline) {
    size_t bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *mask=malloc(bytes);assert(mask);memset(mask,0xff,bytes);
    EpdRect card=card_rect();
    assert(card.y>=UI_LOCK_HEIGHT/2 && card.y<UI_LOCK_HEIGHT);
    epd_fill_rect(card,UI_GRAY_BLACK,mask);
    reader_dict_ui_draw(ctx->fb);
    for(size_t i=0;i<bytes;i++)assert((baseline[i]&mask[i])==(ctx->fb[i]&mask[i]));
    free(mask);
}
static void assert_hold_pixels(app_ctx_t *ctx,const uint8_t *before) {
    assert(reader_dict_ui_holding() && !reader_dict_ui_selecting());
    size_t bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *mask=malloc(bytes);assert(mask);memset(mask,0xff,bytes);
    for(size_t i=0;i<d.n;i++)if(d.cells[i].first==d.hold_first && d.cells[i].end==d.hold_end && d.cells[i].rect.y==d.hold_line) {
        EpdRect r=hold_box(d.cells[i]);if(r.width && r.height)epd_draw_rect(r,UI_GRAY_BLACK,mask);
    }
    int cue_x=d.body.x-10,cue_y=d.hold_line+d.cells[d.hold_index].rect.height/2;
    epd_fill_circle(cue_x,cue_y,3,UI_GRAY_BLACK,mask);
    int right_x=d.body.x+d.body.width+10;
    epd_fill_circle(right_x,cue_y,3,UI_GRAY_BLACK,mask);
    uint8_t *expected=malloc(bytes);assert(expected);memcpy(expected,ctx->fb,bytes);
    epd_fill_circle(cue_x,cue_y,3,UI_GRAY_BLACK,expected);
    epd_fill_circle(right_x,cue_y,3,UI_GRAY_BLACK,expected);
    assert(!memcmp(expected,ctx->fb,bytes));free(expected);
    assert(memcmp(before,ctx->fb,bytes));
    for(size_t i=0;i<bytes;i++)assert((before[i]&mask[i])==(ctx->fb[i]&mask[i]));
    EpdRect area=reader_dict_ui_hold_area(),hint=app_reader.area_hint(ctx);
    assert(area.width>0 && area.height>0 && !memcmp(&area,&hint,sizeof(area)));
    assert(area.x>=d.body.x-13 && area.y>=d.body.y && area.x+area.width<=d.body.x+d.body.width+14);
    assert(area.y+area.height<=(d.card?card_rect().y:d.body.y+d.body.height));
    unsigned refreshes=s_soft_refreshes;app_present(ctx,&app_reader,APP_REDRAW_AREA);assert(s_soft_refreshes==refreshes);
    free(mask);
}
typedef struct { size_t first,end; EpdRect glyph; int row; } glyph_probe_t;
static bool find_glyph(const book_layout_glyph_t *glyph,void *context) {
    glyph_probe_t *probe=context;
    if(!probe->glyph.width && glyph->start>=probe->first && glyph->end<=probe->end && glyph->rect.y==probe->row)
        probe->glyph=glyph->rect;
    return true;
}
static EpdRect original_glyph(size_t page,cell_t cell) {
    glyph_probe_t probe={.first=cell.first,.end=cell.end,.row=cell.rect.y};
    assert(book_layout_visit_page(page,body_rect(),s_px,find_glyph,&probe) && probe.glyph.width);
    return probe.glyph;
}
static app_redraw_t dispatch_touch(app_ctx_t *ctx,ui_gesture_t *gesture,int x,int y,int64_t ms,bool pressed,bool released) {
    reader_touch_t touch={.x=x,.y=y,.touched=!released,.count=released?0:1};
    app_ctx_t sample={.touch=&touch,.now_ms=ms,.pressed=pressed,.released=released};
    ui_gesture_event_t event;
    if(!ui_gesture_feed(gesture,&sample,&event))return APP_REDRAW_NONE;
    app_redraw_t redraw=on_gesture(ctx,&event);
    if(event.type==UI_GESTURE_LONG_PRESS && app_reader.allow_long_drag)
        ui_gesture_allow_long_drag(gesture,app_reader.allow_long_drag(ctx));
    return redraw;
}
static void open_dictionary_fixture(app_ctx_t *ctx,const char *text,size_t page) {
    reader_dict_ui_close();
    assert(book_layout_build(text,strlen(text),body_rect(),s_px));
    ui_clear_page(ctx->fb);book_layout_draw_page(ctx->fb,page,body_rect(),s_px);
    assert(reader_dict_ui_open(text,strlen(text),page,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H));
}
static void lookup_fixture(app_ctx_t *ctx,const char *text) {
    open_dictionary_fixture(ctx,text,0);choose_cell(0);
    assert(reader_dict_ui_tap(UI_LOCK_WIDTH*5/6,d.footer+40) && d.card);
}
static void capture_card(app_ctx_t *ctx) {
    strcpy(dict_draw_text,"\n");dict_draw_max_px=0;dict_capture=true;
    dict_card_clearance=true;dict_card_text_limit=reselect_rect().x-12;
    reader_dict_ui_draw(ctx->fb);dict_capture=false;dict_card_clearance=false;
    int height=0;assert(card_lines(NULL,0,&height)==1 && height<=CARD_HEIGHT);
    assert(card_rect().height==height+44+(d.item_count>1?70:0));
    assert(reselect_rect().width>=48 && reselect_rect().height>=48);
    assert(dict_draw_max_px==30 && !strstr(dict_draw_text,"重选") && !strstr(dict_draw_text,"相关词") && !strstr(dict_draw_text,"组合未收录") && !strstr(dict_draw_text,"测试词典") && !strstr(dict_draw_text,"近义词"));
}
static uint32_t notebook_count(const char *query,reader_vocabulary_record_t *out) {
    reader_vocabulary_record_t rows[8];size_t n,total;
    for(size_t skip=0;;skip+=8) {
        assert(reader_vocabulary_page(skip,rows,8,&n,&total));
        for(size_t i=0;i<n;i++)if(!strcmp(rows[i].query,query)) { if(out)*out=rows[i];return rows[i].count; }
        if(skip+n>=total)return 0;
    }
}
static long notebook_bytes(void) {
    FILE *f=fopen("/sdcard/notes/vocabulary.jsonl","rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);fclose(f);return bytes;
}
static void dictionary_test(app_ctx_t *ctx) {
    size_t chapter=s_chapter,page=s_page,total_pages=book_layout_page_count();
    int turns=s_unsaved_turns,px=s_px,weight=ttf_get_weight();
    char old_font[TTF_FONT_PATH_MAX];strlcpy(old_font,ttf_font_path(),sizeof(old_font));
    s_view=VIEW_LIBRARY;
    assert(defer_key(ctx,UI_KEY_3));
    assert(on_key_long(ctx,UI_KEY_3)==APP_REDRAW_PAGE);
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE);
    s_view=VIEW_READING;s_lookup_mode=true;
    assert(defer_key(ctx,UI_KEY_3) && !defer_key(ctx,UI_KEY_1) && !defer_key(ctx,UI_KEY_2));
    s_menu=true;assert(!defer_key(ctx,UI_KEY_3));s_menu=false;
    s_force_full=false;s_presented_surface=SURFACE_BODY;s_dict_was_selecting=false;
    render(ctx,ctx->fb);
    // 这一个现存像素也必须保留，证明没有重画正文。/ Preserve even this existing pixel to prove no body redraw occurs.
    epd_draw_pixel(3,3,UI_GRAY_BLACK,ctx->fb);
    size_t bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *baseline=malloc(bytes);assert(baseline);memcpy(baseline,ctx->fb,bytes);
    dict_preview(ctx,"reading-original");
    // 截图可能调用render，故重新加上哨兵。/ Screenshot rendering may redraw, so replace the sentinel.
    epd_draw_pixel(3,3,UI_GRAY_BLACK,ctx->fb);memcpy(baseline,ctx->fb,bytes);
    for(size_t fail=0;fail<2;fail++) {
        dict_alloc_remaining=fail;
        assert((reader_dict_ui_open(s_text,s_text_len,s_page,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H)?APP_REDRAW_PAGE:APP_REDRAW_NONE)==APP_REDRAW_NONE && !reader_dict_ui_active());
        assert(!d.snapshot && !d.cells && !d.entry && !memcmp(ctx->fb,baseline,bytes));
    }
    dict_alloc_remaining=SIZE_MAX;
    assert((reader_dict_ui_open(s_text,s_text_len,s_page,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H)?APP_REDRAW_PAGE:APP_REDRAW_NONE)==APP_REDRAW_PAGE && reader_surface()==SURFACE_DICTIONARY);
    assert(d.snapshot && d.cells && !memcmp(d.snapshot,baseline,bytes) && !s_force_full);
    assert(defer_key(ctx,UI_KEY_3));
    s_soft_refreshes=APP_GC16_EVERY-1;
    assert(present(ctx,APP_REDRAW_PAGE) && s_soft_refreshes==APP_GC16_EVERY-1);
    assert_overlay_pixels(ctx,baseline);dict_preview(ctx,"selection-empty");
    size_t mountain=cell_for("山川"),river=mountain+1;
    EpdRect mountain_glyph=original_glyph(page,d.cells[mountain]);
    choose_cell(mountain);choose_cell(river);assert(!d.entry && !d.card);
    assert(present(ctx,APP_REDRAW_PAGE) && s_soft_refreshes==APP_GC16_EVERY-1);
    assert_overlay_pixels(ctx,baseline);dict_preview(ctx,"selection-original-page");
    size_t next_line=1;while(next_line<d.n && d.cells[next_line].rect.y==d.cells[0].rect.y)++next_line;
    assert(next_line<d.n);
    choose_cell(mountain);choose_cell(river);
    choose_units(next_line-1,next_line);
    assert_overlay_pixels(ctx,baseline);dict_preview(ctx,"selection-across-lines");
    // 反复全刷仍只覆盖选区，不重新排版。/ Repeated full refreshes still overlay only the selection without relayout.
    assert_overlay_pixels(ctx,baseline);
    assert(on_tick(ctx)==APP_REDRAW_NONE && book_layout_page_count()==total_pages);
    s_soft_refreshes=3;
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && !reader_dict_ui_active());
    assert(present(ctx,APP_REDRAW_PAGE));
    assert(test_reader_refresh_mode==MODE_GL16 && !test_reader_refresh_area && s_soft_refreshes==4);
    assert(!memcmp(ctx->fb,baseline,bytes));
    assert(!d.snapshot && !d.cells && !d.entry && !d.related);dict_preview(ctx,"selection-restored");
    memcpy(ctx->fb,baseline,bytes);

    ui_gesture_t gesture={0};
    int gx=mountain_glyph.x+mountain_glyph.width/2,gy=mountain_glyph.y+mountain_glyph.height/2;
    unsigned before_hold=dict_lookup_calls;
    assert(dispatch_touch(ctx,&gesture,gx,gy,0,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,gx,gy,500,false,false)==APP_REDRAW_AREA);
    assert(dict_lookup_calls==before_hold && reader_dict_ui_holding());assert_hold_pixels(ctx,baseline);
    assert(dispatch_touch(ctx,&gesture,gx,gy,760,false,true)==APP_REDRAW_PAGE);
    assert(reader_surface()==SURFACE_DICTIONARY && d.card && !d.message && !strcmp(d.query,"山"));
    assert_card_pixels(ctx,baseline);dict_preview(ctx,"direct-card");
    d.save_failed=true;capture_card(ctx);assert(strstr(dict_draw_text,"未保存"));dict_preview(ctx,"card-save-failed-icon");d.save_failed=false;
    assert(!reader_dict_ui_tap(UI_LOCK_WIDTH/2,d.footer+42) && d.card);
    // 卡片开着时可连续长按正文，空白、卡片和取消手势保留原释义。
    // While a card is open, holds on body glyphs replace it; blank, card and cancelled holds keep it.
    EpdRect river_glyph=original_glyph(page,d.cells[river]);
    int rx=river_glyph.x+river_glyph.width/2,ry=river_glyph.y+river_glyph.height/2;
    assert(ry<card_rect().y);
    uint8_t *original_snapshot=d.snapshot;
    ui_gesture_t repeat_gesture={0};
    for(unsigned repeat=0;repeat<12;repeat++) {
        bool to_river=(repeat%2)==0;
        int x=to_river?rx:gx,y=to_river?ry:gy;
        unsigned calls=dict_lookup_calls;uint64_t t=repeat*1000;
        assert(dispatch_touch(ctx,&repeat_gesture,x,y,t,true,false)==APP_REDRAW_NONE);
        assert(dispatch_touch(ctx,&repeat_gesture,x,y,t+500,false,false)==APP_REDRAW_AREA);
        assert(dict_lookup_calls==calls);
        assert(dispatch_touch(ctx,&repeat_gesture,x,y,t+600,false,true)==APP_REDRAW_PAGE);
        assert(d.card && !strcmp(d.query,to_river?"川":"山"));
        assert(d.snapshot==original_snapshot && !memcmp(d.snapshot,baseline,bytes));
        assert_card_pixels(ctx,baseline);
        assert(s_page==page && s_chapter==chapter && s_unsaved_turns==turns);
    }
    unsigned unchanged=dict_lookup_calls;
    EpdRect held_card=card_rect();
    EpdRect icon=reselect_rect();
    const int ignored[][2]={{1,gy},{gx,held_card.y+2},{UI_LOCK_WIDTH/2,d.footer+42},{icon.x+24,icon.y+24}};
    for(size_t i=0;i<sizeof(ignored)/sizeof(*ignored);i++) {
        ui_gesture_t ignored_gesture={0};
        assert(dispatch_touch(ctx,&ignored_gesture,ignored[i][0],ignored[i][1],0,true,false)==APP_REDRAW_NONE);
        assert(dispatch_touch(ctx,&ignored_gesture,ignored[i][0],ignored[i][1],600,false,true)==APP_REDRAW_NONE);
        assert(d.card && !strcmp(d.query,"山") && dict_lookup_calls==unchanged);
    }
    ui_gesture_event_t cancelled_hold={.type=UI_GESTURE_CANCEL,.x0=rx,.y0=ry};
    assert(on_gesture(ctx,&cancelled_hold)==APP_REDRAW_NONE && dict_lookup_calls==unchanged);
    assert(dispatch_touch(ctx,&gesture,icon.x+24,icon.y+24,800,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,icon.x+24,icon.y+24,850,false,true)==APP_REDRAW_PAGE);
    assert(reader_dict_ui_selecting() && !d.selected_count);
    assert_overlay_pixels(ctx,baseline);dict_preview(ctx,"reselect-boxes");
    uint8_t *empty_boxes=malloc(bytes);assert(empty_boxes);memcpy(empty_boxes,ctx->fb,bytes);
    choose_cell(mountain);assert_overlay_pixels(ctx,baseline);
    assert(memcmp(empty_boxes,ctx->fb,bytes));
    choose_cell(mountain);assert_overlay_pixels(ctx,baseline);
    assert(!d.selected_count && !memcmp(empty_boxes,ctx->fb,bytes));
    dict_preview(ctx,"toggle-off");free(empty_boxes);
    choose_cell(mountain);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.card && !strcmp(d.query,"山"));
    assert_card_pixels(ctx,baseline);dict_preview(ctx,"confirmed-card");
    EpdRect card=card_rect();
    assert(dispatch_touch(ctx,&gesture,620,card.y,900,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,620,card.y,950,false,true)==APP_REDRAW_NONE && d.card);
    assert(dispatch_touch(ctx,&gesture,620,card.y-1,1000,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,620,card.y-1,1050,false,true)==APP_REDRAW_PAGE);
    assert(!reader_dict_ui_active() && s_dict_restored && !memcmp(ctx->fb,baseline,bytes));
    assert(s_chapter==chapter && s_page==page && s_unsaved_turns==turns);
    dict_preview(ctx,"dismissed-reading");
    s_dict_restored=false;
    assert(dispatch_touch(ctx,&gesture,gx,gy,1100,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,gx,gy,1600,false,true)==APP_REDRAW_PAGE && d.card);
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && !reader_dict_ui_active());
    assert(!memcmp(ctx->fb,baseline,bytes));
    s_dict_restored=false;
    before_hold=dict_lookup_calls;
    assert(dispatch_touch(ctx,&gesture,1,gy,1700,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,1,gy,2200,false,true)==APP_REDRAW_NONE);
    assert(dict_lookup_calls==before_hold && !reader_dict_ui_active() && s_page==page);
    free(baseline);
    // 词卡重选也包含英文常见词，逐项可选和反选。
    // Card reselection includes function words, each selectable and reversible.
    open_dictionary_fixture(ctx,"I the by under in of THE Under a an and or to for from with",0);
    uint8_t *unboxed=malloc(bytes);assert(unboxed);memset(unboxed,0xff,bytes);
    draw_selection_boxes(unboxed);
    bool any_box=false;for(size_t i=0;i<bytes;i++)if(unboxed[i]!=0xff)any_box=true;assert(any_box);
    free(unboxed);
    for(size_t i=0;i<d.n;i++) {
        EpdRect r=d.cells[i].rect;
        assert(reader_dict_ui_tap(r.x+r.width/2,r.y+r.height/2) && d.selected_count==1);
        assert(reader_dict_ui_tap(r.x+r.width/2,r.y+r.height/2) && d.selected_count==0);
    }
    const char *function_words="the     quiet";
    open_dictionary_fixture(ctx,function_words,0);
    size_t the=cell_for("the"),quiet=cell_for("quiet");
    EpdRect the_glyph=original_glyph(0,d.cells[the]);
    int gap=(d.cells[the].rect.x+d.cells[the].rect.width+d.cells[quiet].rect.x)/2;
    reader_dict_ui_close();
    assert(!reader_dict_ui_open_at(function_words,strlen(function_words),0,body_rect(),s_px,ctx->fb,
        UI_LOCK_HEIGHT-READ_FOOTER_H,gap,the_glyph.y+the_glyph.height/2));
    assert(reader_dict_ui_open_at(function_words,strlen(function_words),0,body_rect(),s_px,ctx->fb,
        UI_LOCK_HEIGHT-READ_FOOTER_H,the_glyph.x+the_glyph.width/2,the_glyph.y+the_glyph.height/2));
    assert(d.card && !d.message && !strcmp(d.query,"the") && !strcmp(d.entry->word,"the"));
    open_dictionary_fixture(ctx,"一心一意",0);
    choose_units(0,3);choose_cell(1);
    unsigned before_hole=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.card && d.message && d.selected_count==3);
    assert(dict_lookup_calls==before_hole);
    choose_cell(1);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.card && !strcmp(d.query,"一心一意"));
    open_dictionary_fixture(ctx,"quiet, reader",0);
    choose_units(0,d.n-1);
    before_hole=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.card && d.message && dict_lookup_calls==before_hole);
    open_dictionary_fixture(ctx,"quiet reader pages theory underline",0);
    choose_units(0,3);choose_cell(4);
    assert(d.message && d.selected_count==4);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.card && !strcmp(d.query,"quiet reader pages theory"));
    open_dictionary_fixture(ctx,"quiet the reader",0);
    choose_cell(cell_for("quiet"));choose_cell(cell_for("the"));choose_cell(cell_for("reader"));
    assert(d.selected_count==3);
    assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"quiet the reader") && d.item_count==4);
    open_dictionary_fixture(ctx,"quiet the reader of pages",0);
    choose_cell(cell_for("quiet"));choose_cell(cell_for("reader"));choose_cell(cell_for("pages"));
    before_hole=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.card && d.message && dict_lookup_calls==before_hole);
    open_dictionary_fixture(ctx,"theory inside underline mother-in-law",0);
    choose_units(0,d.n-1);
    assert(!d.message && d.selected_count==4);
    const char *example="山川 quiet reader co-operate don't quiet";
    open_dictionary_fixture(ctx,example,0);
    choose_cell(0);choose_cell(1);assert(reader_dict_ui_tap(600,d.footer+24));
    assert(d.fallback && !d.message && !strcmp(d.entry->word,"山"));
    s_soft_refreshes=3;assert(present(ctx,APP_REDRAW_PAGE) && s_soft_refreshes==4);
    assert(readable_pronunciation(d.entry->pronunciation));dict_preview(ctx,"character");
    assert(reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && !strcmp(d.message,"未收录"));
    assert(reader_dict_ui_tap(80,UI_LOCK_HEIGHT-32) && !d.message);reselect_fixture();
    assert(present(ctx,APP_REDRAW_PAGE) && s_soft_refreshes==5);
    choose_cell(cell_for("quiet"));choose_cell(cell_for("reader"));
    assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"quiet reader") && d.fallback && !d.message && !strcmp(d.entry->word,"quiet"));
    assert(reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && !strcmp(d.message,"未收录"));
    reselect_fixture();choose_cell(cell_for("co-operate"));
    assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"co-operate") && !d.message && d.item_count==3 && !d.item);
    assert(reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && !d.message && !strcmp(d.entry->word,"co"));
    assert(reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && !d.message && !strcmp(d.entry->word,"operate"));
    reselect_fixture();
    choose_cell(cell_for("don't"));assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"don't"));
    reselect_fixture();choose_cell(cell_for("quiet"));
    assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"quiet") && !d.message && d.entry->count==2);
    assert(!readable_pronunciation(d.entry->pronunciation));dict_preview(ctx,"english-definition");
    assert(!reader_dict_ui_tap(600,d.footer+24) && !reader_dict_ui_tap(80,d.footer+24));
    capture_card(ctx);assert(!strstr(dict_draw_text,"近义词") && strstr(dict_draw_text,"…"));
    reselect_fixture();
    assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/offline.hidden"));
    choose_cell(cell_for("quiet"));
    assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.message,"未找到词典"));
    assert(!rename("/sdcard/dict/offline.hidden","/sdcard/dict/offline.dict"));
    reader_dict_ui_close();
    const char *long_four="abcdefghijklmnopqrst uvwxyzabcdefghijklmn opqrstuvwxyzabcdefgh ijklmnopqrstuvwxyzab";
    open_dictionary_fixture(ctx,long_four,0);choose_units(0,d.n-1);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.card && d.fallback && strlen(d.query)>63);
    assert(d.item_count==5);
    lookup_fixture(ctx,"走");assert(!d.message && d.entry->count==5 && !strcmp(d.entry->pronunciation,"zǒu"));
    assert(card_lines(NULL,0,NULL)==1);dict_preview(ctx,"simplified-zou-first");
    capture_card(ctx);assert(strstr(dict_draw_text,"1. 步行") && !strstr(dict_draw_text,"5."));
    assert(!reader_dict_ui_tap(600,d.footer+24));
    lookup_fixture(ctx,"行");assert(!d.message && !strcmp(d.entry->senses[1].pronunciation,"háng"));
    dict_preview(ctx,"simplified-polyphone");
    open_dictionary_fixture(ctx,"一心一意",0);choose_cell(1);
    unsigned calls_before=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.message && !strcmp(d.entry->word,"心"));
    assert(d.item_count==1 && !d.item && d.related && !strcmp(d.related->word,"一心一意") && !d.fallback);
    assert(dict_lookup_calls-calls_before<=10);
    calls_before=dict_lookup_calls;capture_card(ctx);reader_dict_ui_draw(ctx->fb);
    assert(dict_lookup_calls==calls_before && strstr(dict_draw_text,"心（") && (strstr(dict_draw_text,"一心一意") && !strstr(dict_draw_text,"一心一意（") && !strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn")));
    assert(strstr(dict_draw_text,"心脏") && strstr(dict_draw_text,"心思专一"));
    assert(!reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && !reader_dict_ui_tap(600,d.footer+24));
    open_dictionary_fixture(ctx,"一心一意",0);choose_cell(1);choose_cell(2);
    calls_before=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && d.fallback && !strcmp(d.entry->word,"心"));
    assert(d.item_count==3 && d.related && dict_lookup_calls-calls_before<=7);
    capture_card(ctx);assert(strstr(dict_draw_text,"心（") && (strstr(dict_draw_text,"一心一意") && !strstr(dict_draw_text,"一心一意（") && !strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn")));
    assert(reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32) && d.message);
    capture_card(ctx);assert(strstr(dict_draw_text,"未收录") && (strstr(dict_draw_text,"一心一意") && !strstr(dict_draw_text,"一心一意（") && !strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn")));
    assert(!reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32));
    open_dictionary_fixture(ctx,"一心一意",0);choose_cell(3);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.message && !d.item && !d.fallback && d.item_count==1 && d.related);
    capture_card(ctx);assert(strstr(dict_draw_text,"意  未收录") && strstr(dict_draw_text,"未收录") && (strstr(dict_draw_text,"一心一意") && !strstr(dict_draw_text,"一心一意（") && !strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn")));
    assert(!reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32));
    const char *blocked_contexts[]={"一心，一意","一 心一意","心心意意"};
    for(size_t i=0;i<3;i++) {
        open_dictionary_fixture(ctx,blocked_contexts[i],0);choose_cell(cell_for("心"));
        assert(reader_dict_ui_tap(600,d.footer+24) && !d.message && d.item_count==1 && !d.related);
    }
    // 最坏情况：九个相关词候选全未收录，再查原单字，共十次。/ Worst case: nine related candidates miss, followed by the original character, for ten lookups.
    open_dictionary_fixture(ctx,"山川山心川山川",0);choose_cell(3);
    calls_before=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.message && !strcmp(d.entry->word,"心"));
    assert(d.item_count==1 && !d.related && dict_lookup_calls-calls_before==10);
    open_dictionary_fixture(ctx,"一心一意",0);choose_units(0,3);
    calls_before=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.message && !strcmp(d.entry->word,"一心一意"));
    assert(d.item_count==1 && !d.related && dict_lookup_calls-calls_before==1);
    capture_card(ctx);assert(!strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn"));
    open_dictionary_fixture(ctx,"山川之间",0);choose_units(0,3);
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.message);
    capture_card(ctx);assert(strstr(dict_draw_text,"山川之间（"));
    lookup_fixture(ctx,"旧");assert(d.message && !strcmp(d.message,"词典文件损坏"));
    // 可见行之间的连字符是排版产物，查询仍取原文完整英文词。/ Wrapping hyphens are display artifacts; queries use the complete original word.
    const char *wrapped="山川山川山川山川山川山川山 extraordinary 山川山川山川山川山川";
    open_dictionary_fixture(ctx,wrapped,0);
    size_t fragment=cell_for("extraordinary");
    assert(fragment+1<d.n && d.cells[fragment].first==d.cells[fragment+1].first && d.cells[fragment].rect.y!=d.cells[fragment+1].rect.y);
    EpdRect wrap_glyphs[]={original_glyph(0,d.cells[fragment]),original_glyph(0,d.cells[fragment+1])};
    choose_cell(fragment);choose_cell(fragment+1);
    assert(!d.selected_count && !reader_dict_ui_tap(600,d.footer+24));
    choose_cell(fragment);assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"extraordinary"));
    reselect_fixture();choose_cell(fragment+1);assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"extraordinary"));
    for(size_t i=0;i<2;i++) {
        EpdRect glyph=wrap_glyphs[i];
        reader_dict_ui_close();
        assert(reader_dict_ui_open_at(wrapped,strlen(wrapped),0,body_rect(),s_px,ctx->fb,
            UI_LOCK_HEIGHT-READ_FOOTER_H,glyph.x+glyph.width/2,glyph.y+glyph.height/2));
        assert(d.card && !strcmp(d.query,"extraordinary"));
    }
    // 按住词中任何字形都应查完整带重音词。/ Holding any glyph must look up the whole accented word.
    const struct { const char *word; size_t offsets[3]; } accented[]={
        {"résumé",{4,1,6}}, {"café",{2,4,3}}, {"pré-payé",{4,5,8}}, {"l’été",{6,4,1}},
    };
    for(size_t i=0;i<sizeof(accented)/sizeof(*accented);i++) {
        const char *word=accented[i].word;
        open_dictionary_fixture(ctx,word,0);
        for(size_t j=0;j<3;j++) {
            size_t off=accented[i].offsets[j];
            EpdRect glyph=original_glyph(0,(cell_t){.first=off,.end=off+step(word+off,strlen(word)-off),
                .rect.y=body_rect().y});
            reader_dict_ui_close();
            assert(reader_dict_ui_open_at(word,strlen(word),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,
                glyph.x+glyph.width/2,glyph.y+glyph.height/2));
            assert(d.card && !strcmp(d.query,word));
        }
        reselect_fixture();
        assert(d.n==1);
        choose_cell(0);
        assert(reader_dict_ui_tap(600,d.footer+24) && d.card && !strcmp(d.query,word));
    }
    const char *variants[]={"co‐operate","co‑operate","co\xc2\xadoperate","don’t"};
    const char *normalized[]={"co-operate","co-operate","cooperate","don’t"};
    for(size_t i=0;i<4;i++) {
        lookup_fixture(ctx,variants[i]);assert(!strcmp(d.query,normalized[i]));
        assert(d.selected_count==1);
        assert(d.item_count==(i<2?3:1));
    }
    // 页边界不会扩张，不能跳到重排出来的新选字页。/ Page boundaries never expand and there is no separately repaginated selection page.
    char many[4096]="";for(int i=0;i<200;i++)strcat(many,"山川 quiet ");
    open_dictionary_fixture(ctx,many,1);
    assert(d.start==book_layout_page_start_offset(1) && d.end==book_layout_page_start_offset(2));
    for(size_t i=0;i<d.n;i++)assert(d.cells[i].rect.y>=body_rect().y && d.cells[i].rect.y<d.footer);
    choose_cell(0);choose_cell(d.n-1);
    before_hole=dict_lookup_calls;
    assert(reader_dict_ui_tap(600,d.footer+24) && !d.card && d.message && dict_lookup_calls==before_hole);
    choose_cell(d.n-1);choose_units(1,3);
    assert(!d.message && d.selected_count==4);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.card && d.fallback);
    reselect_fixture();
    assert(reader_dict_ui_tap(80,d.footer+24) && !reader_dict_ui_active());
    assert(!d.snapshot && !d.cells && !d.entry && !d.related);
    reader_dict_ui_close();
    EpdRect short_body=body_rect();short_body.height=72;
    assert(book_layout_build(wrapped,strlen(wrapped),short_body,s_px));
    size_t edge=book_layout_page_start_offset(1);
    assert(edge && isalpha((unsigned char)wrapped[edge-1]) && isalpha((unsigned char)wrapped[edge]));
    for(size_t p=0;p<2;p++) {
        ui_clear_page(ctx->fb);book_layout_draw_page(ctx->fb,p,short_body,s_px);
        assert(reader_dict_ui_open(wrapped,strlen(wrapped),p,short_body,s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H));
        choose_cell(p?0:d.n-1);
        cell_t chosen=d.cells[d.selection[0]];
        assert(p?chosen.first<d.start:chosen.end>d.end);
        assert(reader_dict_ui_tap(600,d.footer+24) && !strcmp(d.query,"extraordinary"));
        reader_dict_ui_close();
    }
    if(getenv("READER_SIMPLIFIED_SAMPLE")) {
        assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/fixture.dict"));
        assert(!rename("/sdcard/dict/candidate.dict","/sdcard/dict/offline.dict"));
        const char *words[]={"走","山","行","重","quiet","bank","read"};
        const char *names[]={"candidate-zou","candidate-shan","candidate-xing","candidate-zhong","candidate-quiet","candidate-bank","candidate-read"};
        char passage[2048]="山川之间\n";
        for(int i=0;i<10;i++)strcat(passage,"行走在山川之间，重新看见熟悉的风景。The quiet reader turns another page. I read by the river bank.\n");
        for(size_t i=0;i<7;i++) {
            open_dictionary_fixture(ctx,passage,0);choose_cell(cell_for(words[i]));
            assert(reader_dict_ui_tap(600,d.footer+24));
            if(d.message)fprintf(stderr,"candidate %s: %s\n",words[i],d.message);
            assert(!d.message);dict_preview(ctx,names[i]);
        }
        // 用户反馈的英文原样进入真实选词链路，核对原词与清理后的释义。
        // Exercise reported English forms through real token selection and cleaned definitions.
        if(getenv("READER_ENGLISH_SAMPLE")) {
            const char *forms[]={"vastness","condense","unconscious","fragments","combining","differently","circulation","jurisdiction’s"};
            const char *heads[]={"vastness","condense","unconscious","fragment","combine","differently","circulation","jurisdiction"};
            for(size_t i=0;i<sizeof(forms)/sizeof(*forms);i++) {
                char text[512];snprintf(text,sizeof(text),"%s. The reader pauses to understand this word. Reading continues on the same page.",forms[i]);
                lookup_fixture(ctx,text);
                assert(!d.message && d.entry && !strcmp(d.entry->word,heads[i]));
                capture_card(ctx);assert(!strstr(dict_draw_text,"︁") && !strstr(dict_draw_text,"未收录"));
                if(i==6)dict_preview(ctx,"candidate-circulation");
                if(i==7)dict_preview(ctx,"candidate-possessive");
            }
        }
        char idiom_passage[2048]="一心一意地读书，慢慢理解书里的故事。\n";
        for(int i=0;i<10;i++)strcat(idiom_passage,"行走在山川之间，重新看见熟悉的风景。The quiet reader turns another page. I read by the river bank.\n");
        open_dictionary_fixture(ctx,idiom_passage,0);choose_cell(cell_for("心"));
        assert(reader_dict_ui_tap(600,d.footer+24) && !d.message && !strcmp(d.entry->word,"心"));
        assert(d.item_count==1 && d.related && !strcmp(d.related->word,"一心一意"));
        capture_card(ctx);assert(strstr(dict_draw_text,"心（") && (strstr(dict_draw_text,"一心一意") && !strstr(dict_draw_text,"一心一意（") && !strstr(dict_draw_text,"yī xīn") && !strstr(dict_draw_text,"yīxīn")));
        assert(!reader_dict_ui_tap(600,UI_LOCK_HEIGHT-32));dict_preview(ctx,"candidate-character-and-idiom");
        reader_dict_ui_close();
        assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/candidate.dict"));
        assert(!rename("/sdcard/dict/fixture.dict","/sdcard/dict/offline.dict"));
    }
    assert(!strcmp(old_font,ttf_font_path()) && s_px==px && ttf_get_weight()==weight);
    assert(s_chapter==chapter && s_page==page && turns==s_unsaved_turns);
    assert(relayout());s_dict_restored=false;
    // 只记录用户确认的字词，相关词探测、回看和绘图均不计次。/ Only explicit queries count; related probes, review and rendering do not.
    uint32_t heart=notebook_count("心",NULL),idiom=notebook_count("一心一意",NULL);
    reader_dict_ui_set_book("单词本来源书");
    open_dictionary_fixture(ctx,"一心一意",0);choose_cell(1);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.related);
    reader_vocabulary_record_t stored;
    assert(notebook_count("心",&stored)==heart+1 && !strcmp(stored.book,"单词本来源书"));
    assert(notebook_count("一心一意",NULL)==idiom);
    long saved_bytes=notebook_bytes();
    capture_card(ctx);reader_dict_ui_draw(ctx->fb);
    assert(notebook_bytes()==saved_bytes && notebook_count("心",NULL)==heart+1);
    uint32_t absent=notebook_count("心一",NULL);
    open_dictionary_fixture(ctx,"一心一意",0);choose_cell(1);choose_cell(2);
    assert(reader_dict_ui_tap(600,d.footer+24) && d.fallback);
    assert(notebook_count("心一",&stored)==absent+1 && stored.result==READER_DICT_NOT_FOUND);
    assert(notebook_count("心",NULL)==heart+1 && notebook_count("一心一意",NULL)==idiom);
    reader_dict_ui_close();
    assert(relayout());s_dict_restored=false;reader_dict_ui_set_book(s_book_title);
    puts("dictionary overlay: direct release, box toggles, exact source spans, card pixels, and reader restore passed");
}
static void usb_preview(app_ctx_t* ctx,const char* name) {
    const char* directory=getenv("READER_TOC_PREVIEW");if(!directory)return;
    char path[512];usb_render(ctx,ctx->fb);
    assert(reader_screenshot_write(directory,name,ctx->fb,1216,684,path,sizeof(path)));
}
typedef struct { book_layout_glyph_t glyphs[128];size_t count; } hold_glyphs_t;
static bool hold_collect_glyph(const book_layout_glyph_t *glyph,void *context) {
    hold_glyphs_t *all=context;if(all->count>=128)return false;all->glyphs[all->count++]=*glyph;return true;
}
static void small_hold_geometry_test(app_ctx_t *ctx) {
    int old_px=s_px;size_t old_page=s_page,old_len=s_text_len;char *old_text=s_text;s_px=40;
    char text[]="quiet reader.\n山川在眼前。\nquiet reader";
    assert(book_layout_build(text,strlen(text),body_rect(),s_px));
    hold_glyphs_t all={0};assert(book_layout_visit_page(0,body_rect(),s_px,hold_collect_glyph,&all));
    ui_clear_page(ctx->fb);book_layout_draw_page(ctx->fb,0,body_rect(),s_px);
    size_t bytes=(size_t)epd_width()*epd_height()/2;uint8_t *base=malloc(bytes),*prior=malloc(bytes),*held=malloc(bytes);
    assert(base && prior && held);memcpy(base,ctx->fb,bytes);
    int x=-1,y=-1;
    for(size_t i=1;i<all.count && all.glyphs[i].start<5;i++) {
        EpdRect left=all.glyphs[i-1].rect,right=all.glyphs[i].rect;
        if(left.y==right.y && right.x>left.x+left.width) { x=left.x+left.width;y=left.y+left.height/2;break; }
    }
    assert(x>=0);
    for(size_t i=0;i<all.count;i++)assert(!ui_rect_hit(all.glyphs[i].rect,x,y));
    fprintf(stderr,"small hold repro: font40, inter-letter gap=(%d,%d), no exact glyph hit\n",x,y);
    assert(reader_dict_ui_open_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,x,y));
    assert(d.card && !strcmp(d.query,"quiet"));reader_dict_ui_close();
    EpdRect letter=all.glyphs[0].rect;
    assert(reader_dict_ui_open_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,
        letter.x+letter.width/2,letter.y+letter.height+2));
    assert(!strcmp(d.query,"quiet"));reader_dict_ui_close();
    EpdRect reader={0},period={0},word={0};
    for(size_t i=0;i<all.count;i++) {
        if(all.glyphs[i].start==6)reader=all.glyphs[i].rect;
        if(all.glyphs[i].start==12)period=all.glyphs[i].rect;
        if(all.glyphs[i].start<5)word=word.width?ui_rect_union(word,all.glyphs[i].rect):all.glyphs[i].rect;
    }
    assert(reader.width && period.width);
    int gap=(word.x+word.width+reader.x-1)/2;
    assert(reader_dict_ui_open_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,gap,y));
    int left_distance=gap-(word.x+word.width-1),right_distance=reader.x-gap;
    assert(!strcmp(d.query,left_distance<=right_distance?"quiet":"reader"));reader_dict_ui_close();
    const int misses[][2]={{period.x+period.width/2,period.y+period.height/2},{1,y},{600,600},{x,UI_LOCK_HEIGHT-8}};
    unsigned calls=dict_lookup_calls;
    for(size_t i=0;i<sizeof(misses)/sizeof(*misses);i++) {
        assert(!reader_dict_ui_open_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,misses[i][0],misses[i][1]));
        assert(dict_lookup_calls==calls && !memcmp(ctx->fb,base,bytes));
    }
    // 等距取阅读顺序；更近的后字优先，精确标点阻挡，卡下隐藏字形不参与。/ Tie by reading order, prefer a closer later glyph, let exact punctuation block, and exclude hidden glyphs.
    book_layout_glyph_t a={.start=0,.end=1,.rect={100,100,10,10},.px=40},b={.start=1,.end=2,.rect={123,100,10,10},.px=40};
    glyph_scan_t scan={.direct=true,.hit=SIZE_MAX,.x=116,.y=105,.bounds={0,0,200,200}};
    assert(count_glyph(&a,&scan) && count_glyph(&b,&scan) && scan.first==0 && scan.distance==49);
    scan=(glyph_scan_t){.direct=true,.hit=SIZE_MAX,.x=117,.y=105,.bounds={0,0,200,200}};
    assert(count_glyph(&a,&scan) && count_glyph(&b,&scan) && scan.first==1);
    b.rect=(EpdRect){100,123,10,10};scan=(glyph_scan_t){.direct=true,.hit=SIZE_MAX,.x=105,.y=116,.bounds={0,0,200,200}};
    assert(count_glyph(&a,&scan) && count_glyph(&b,&scan) && scan.first==0);
    b.rect=(EpdRect){100,110,10,10};scan=(glyph_scan_t){.direct=true,.hit=SIZE_MAX,.x=105,.y=109,.bounds={0,0,200,110}};
    assert(count_glyph(&b,&scan) && scan.hit==SIZE_MAX);
    scan=(glyph_scan_t){.direct=true,.hit=SIZE_MAX,.x=121,.y=115,.bounds={0,0,200,200}};
    assert(count_glyph(&a,&scan) && scan.hit==SIZE_MAX);

    s_text=text;s_text_len=strlen(text);s_page=0;
    uint32_t queries=notebook_count("quiet",NULL);calls=dict_lookup_calls;
    ui_gesture_t hold={0};
    assert(dispatch_touch(ctx,&hold,x,y,0,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&hold,x,y,500,false,false)==APP_REDRAW_AREA);
    assert(d.hold_first==0 && d.hold_end==5 && dict_lookup_calls==calls && notebook_count("quiet",NULL)==queries);
    assert_hold_pixels(ctx,base);assert(reader_dict_ui_hold_area().height<100);
    dict_preview(ctx,"hold-smallfont-feedback");
    memcpy(held,ctx->fb,bytes);dict_pending_frame=held;dict_pending_fb=ctx->fb;dict_pending_bytes=bytes;
    assert(dispatch_touch(ctx,&hold,x,y,550,false,true)==APP_REDRAW_PAGE);dict_pending_frame=NULL;
    assert(d.card && !d.holding && !strcmp(d.query,"quiet") && dict_lookup_calls==calls+1 && notebook_count("quiet",NULL)==queries+1);
    assert_card_pixels(ctx,base);dict_preview(ctx,"hold-smallfont-definition");
    memcpy(prior,ctx->fb,bytes);uint8_t *snapshot=d.snapshot;calls=dict_lookup_calls;
    int rx=reader.x-1,ry=reader.y+reader.height/2;
    assert(dispatch_touch(ctx,&hold,rx,ry,1000,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&hold,rx,ry,1500,false,false)==APP_REDRAW_AREA);
    assert(d.card && d.snapshot==snapshot && !strcmp(d.query,"quiet") && d.hold_first==6 && dict_lookup_calls==calls);
    assert_hold_pixels(ctx,prior);dict_preview(ctx,"hold-existing-card-feedback");
    EpdRect dirty=app_reader.area_hint(ctx),clip=hold_box((cell_t){.rect={40,card_rect().y-5,40,20}});
    assert(clip.height>0 && clip.y+clip.height==card_rect().y);
    ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};
    assert(on_gesture(ctx,&cancel)==APP_REDRAW_DONE && !d.holding && d.card && d.snapshot==snapshot);
    assert(test_reader_refresh_mode==MODE_GL16 && test_reader_refresh_area && !memcmp(&dirty,&test_reader_refresh_rect,sizeof(dirty)));
    assert(!memcmp(ctx->fb,prior,bytes) && dict_lookup_calls==calls);
    EpdRect after=app_reader.area_hint(ctx);assert(!memcmp(&dirty,&after,sizeof(dirty)));
    reader_dict_ui_close();ui_gesture_reset(&hold);assert(!memcmp(ctx->fb,base,bytes));
    queries=notebook_count("quiet",NULL);calls=dict_lookup_calls;
    for(int kind=0;kind<4;kind++) {
        assert(dispatch_touch(ctx,&hold,x,y,0,true,false)==APP_REDRAW_NONE);
        assert(dispatch_touch(ctx,&hold,x,y,500,false,false)==APP_REDRAW_AREA);assert_hold_pixels(ctx,base);
        dirty=app_reader.area_hint(ctx);
        reader_touch_t touch={.x=x,.y=y,.touched=kind!=2,.count=kind==1?2:kind==2?0:1};
        app_ctx_t invalid={.touch=kind==3?NULL:&touch,.now_ms=600,.consumed=kind==0};
        ui_gesture_event_t event;
        assert(ui_gesture_feed(&hold,&invalid,&event) && event.type==UI_GESTURE_CANCEL);
        assert(on_gesture(ctx,&event)==APP_REDRAW_DONE);
        after=app_reader.area_hint(ctx);
        assert(!reader_dict_ui_active() && !memcmp(&dirty,&after,sizeof(dirty)) && !memcmp(ctx->fb,base,bytes));
        assert(test_reader_refresh_mode==MODE_GL16 && test_reader_refresh_area && !memcmp(&dirty,&test_reader_refresh_rect,sizeof(dirty)));
        unsigned already_refreshed=test_reader_refresh_calls;app_present(ctx,&app_reader,APP_REDRAW_DONE);
        assert(test_reader_refresh_calls==already_refreshed);
        assert(dispatch_touch(ctx,&hold,x,y,650,false,true)==APP_REDRAW_NONE);
        assert(dict_lookup_calls==calls && notebook_count("quiet",NULL)==queries && s_page==0);
    }
    dict_alloc_remaining=0;
    assert(dispatch_touch(ctx,&hold,x,y,0,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&hold,x,y,500,false,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&hold,x,y,550,false,true)==APP_REDRAW_NONE);
    assert(!reader_dict_ui_active() && dict_lookup_calls==calls);dict_alloc_remaining=SIZE_MAX;
    free(base);free(prior);free(held);
    s_text=old_text;s_text_len=old_len;s_page=old_page;s_px=old_px;assert(relayout());s_dict_restored=false;
    puts("hold feedback:40px gaps/line edges, nearest ties, clipping, immediate local DU, no prelookup, once-only release, cancellation and original/card pixels PASS");
}
static void locked_line_hold_test(app_ctx_t *ctx) {
    int old_px=s_px;size_t old_page=s_page,old_len=s_text_len;char *old_text=s_text;
    s_px=40;s_page=0;s_view=VIEW_READING;s_dict_restored=false;
    char text[]="山川一心走行读。\n心山川行走读。";
    s_text=text;s_text_len=strlen(text);
    open_dictionary_fixture(ctx,text,0);
    cell_t first=d.cells[0],second=d.cells[1],last=first;
    for(size_t i=0;i<d.n;i++)if(d.cells[i].rect.y==first.rect.y)last=d.cells[i];
    reader_dict_ui_close();
    size_t bytes=(size_t)epd_width()*epd_height()/2;uint8_t *base=malloc(bytes),*held=malloc(bytes);assert(base && held);
    memcpy(base,ctx->fb,bytes);
    int x=first.rect.x+first.rect.width-1,y=first.rect.y+first.rect.height/2;
    unsigned calls=dict_lookup_calls;uint32_t mountain=notebook_count("山",NULL),river=notebook_count("川",NULL);
    ui_gesture_t gesture={0};
    assert(app_reader.allow_long_drag && !app_reader.allow_long_drag(ctx));
    assert(dispatch_touch(ctx,&gesture,x,y,0,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,x,y,500,false,false)==APP_REDRAW_AREA);
    assert(gesture.long_drag && app_reader.allow_long_drag(ctx));assert_hold_pixels(ctx,base);
    dict_preview(ctx,"hold-locked-line-initial");
    memcpy(held,ctx->fb,bytes);
    assert(dispatch_touch(ctx,&gesture,x+1,UI_LOCK_HEIGHT-2,510,false,false)==APP_REDRAW_NONE);
    assert(d.hold_first==first.first && !memcmp(held,ctx->fb,bytes) && dict_lookup_calls==calls);
    dict_preview(ctx,"hold-locked-line-finger-away");
    int sx=x+(second.rect.x+second.rect.width/2)-(first.rect.x+first.rect.width/2);
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,520,false,false)==APP_REDRAW_AREA && d.hold_first==second.first);
    assert_hold_pixels(ctx,base);
    assert(dict_lookup_calls==calls && notebook_count("山",NULL)==mountain && notebook_count("川",NULL)==river);
    dict_preview(ctx,"hold-locked-line-moved");
    assert(dispatch_touch(ctx,&gesture,sx,1,530,false,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,UI_LOCK_WIDTH-1,1,540,false,false)==APP_REDRAW_AREA && d.hold_first==last.first);
    assert_hold_pixels(ctx,base);
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,550,false,false)==APP_REDRAW_AREA && d.hold_first==second.first);
    assert_hold_pixels(ctx,base);
    memcpy(held,ctx->fb,bytes);dict_pending_frame=held;dict_pending_fb=ctx->fb;dict_pending_bytes=bytes;
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,600,false,true)==APP_REDRAW_PAGE);dict_pending_frame=NULL;
    assert(!strcmp(d.query,"川") && notebook_count("川",NULL)==river+1 && notebook_count("山",NULL)==mountain);
    assert(!reader_dict_ui_hold_finish());assert_card_pixels(ctx,base);dict_preview(ctx,"hold-locked-line-definition");
    memcpy(held,ctx->fb,bytes);uint8_t *original_snapshot=d.snapshot;
    // 从已有卡片外发起，拖到远端再回到近端；取消恢复整条轨迹且保留旧卡。/ Start outside an existing card, move far and back, then restore the whole trail while retaining the old card.
    assert(dispatch_touch(ctx,&gesture,x,y,1000,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,x,y,1500,false,false)==APP_REDRAW_AREA);
    calls=dict_lookup_calls;
    unsigned measures=dict_measure_calls;
    assert(reader_dict_ui_hold_move(UI_LOCK_WIDTH-1,UI_LOCK_HEIGHT-2));
    assert(dict_measure_calls==measures);
    assert(dictionary_hold_redraw(ctx,false)==APP_REDRAW_AREA);
    assert(dispatch_touch(ctx,&gesture,sx,1,1520,false,false)==APP_REDRAW_AREA);
    EpdRect trail=reader_dict_ui_hold_restore_area();
    assert(trail.x<=first.rect.x && trail.x+trail.width>=last.rect.x+last.rect.width);
    ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};
    assert(on_gesture(ctx,&cancel)==APP_REDRAW_DONE && d.card && !d.holding && d.snapshot==original_snapshot);
    assert(test_reader_refresh_mode==MODE_GL16 && !memcmp(&trail,&test_reader_refresh_rect,sizeof(trail)));
    assert(!memcmp(ctx->fb,held,bytes) && dict_lookup_calls==calls);
    reader_dict_ui_close();ui_gesture_reset(&gesture);assert(!memcmp(ctx->fb,base,bytes));
    // 新开的覆盖层取消后也恢复原页；同词的上下移动完全不刷新。/ Cancelling a newly opened overlay also restores the page; vertical movement within one word never refreshes.
    assert(dispatch_touch(ctx,&gesture,x,y,2000,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,x,y,2500,false,false)==APP_REDRAW_AREA);
    assert(dispatch_touch(ctx,&gesture,UI_LOCK_WIDTH-1,1,2510,false,false)==APP_REDRAW_AREA);
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,2520,false,false)==APP_REDRAW_AREA);
    trail=reader_dict_ui_hold_restore_area();
    assert(on_gesture(ctx,&cancel)==APP_REDRAW_DONE && !reader_dict_ui_active());
    assert(!memcmp(ctx->fb,base,bytes) && !memcmp(&trail,&test_reader_refresh_rect,sizeof(trail)) && dict_lookup_calls==calls);
    dict_preview(ctx,"hold-locked-line-cancelled");
    ui_gesture_reset(&gesture);
    char english[]="quiet reader.\nquiet reader";s_text=english;s_text_len=strlen(english);
    open_dictionary_fixture(ctx,english,0);first=d.cells[0];second=d.cells[1];reader_dict_ui_close();memcpy(base,ctx->fb,bytes);
    x=first.rect.x+first.rect.width-1;y=first.rect.y+first.rect.height/2;
    sx=x+(second.rect.x+second.rect.width/2)-(first.rect.x+first.rect.width/2);
    assert(dispatch_touch(ctx,&gesture,x,y,3000,true,false)==APP_REDRAW_NONE);
    assert(dispatch_touch(ctx,&gesture,x,y,3500,false,false)==APP_REDRAW_AREA);
    assert(dispatch_touch(ctx,&gesture,x+2,UI_LOCK_HEIGHT-2,3510,false,false)==APP_REDRAW_NONE && d.hold_first==0);
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,3520,false,false)==APP_REDRAW_AREA && d.hold_first==6 && d.hold_end==12);
    assert_hold_pixels(ctx,base);dict_preview(ctx,"hold-locked-english-moved");
    uint32_t readers=notebook_count("reader",NULL);calls=dict_lookup_calls;
    assert(dispatch_touch(ctx,&gesture,sx,UI_LOCK_HEIGHT-2,3600,false,true)==APP_REDRAW_PAGE && !strcmp(d.query,"reader"));
    assert(dict_lookup_calls==calls+1 && notebook_count("reader",NULL)==readers+1);reader_dict_ui_close();
    EpdRect narrow={30,100,85,900};char wrapped[]="extraordinary quiet";
    assert(book_layout_build(wrapped,strlen(wrapped),narrow,s_px));ui_clear_page(ctx->fb);book_layout_draw_page(ctx->fb,0,narrow,s_px);
    assert(reader_dict_ui_open(wrapped,strlen(wrapped),0,narrow,s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H));
    first=d.cells[0];assert(d.n>1 && d.cells[1].first==first.first && d.cells[1].rect.y!=first.rect.y);
    cell_t wrapped_second=d.cells[1];
    reader_dict_ui_close();memcpy(base,ctx->fb,bytes);
    x=first.rect.x+first.rect.width/2;y=first.rect.y+first.rect.height/2;
    assert(reader_dict_ui_hold_at(wrapped,strlen(wrapped),0,narrow,s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,x,y));
    assert(dictionary_hold_redraw(ctx,false)==APP_REDRAW_AREA);assert_hold_pixels(ctx,base);
    assert(reader_dict_ui_hold_area().height<=first.rect.height+8);
    assert(!reader_dict_ui_hold_move(UI_LOCK_WIDTH-1,UI_LOCK_HEIGHT-2));
    assert(reader_dict_ui_hold_finish() && !strcmp(d.query,"extraordinary"));reader_dict_ui_draw(ctx->fb);
    assert(reader_dict_ui_hold_existing(wrapped_second.rect.x+wrapped_second.rect.width/2,wrapped_second.rect.y+wrapped_second.rect.height/2));
    assert(d.hold_line==wrapped_second.rect.y && d.hold_index==1);
    assert(dictionary_hold_redraw(ctx,false)==APP_REDRAW_AREA);
    assert(reader_dict_ui_hold_area().y>=wrapped_second.rect.y-2);
    assert(reader_dict_ui_hold_finish() && !strcmp(d.query,"extraordinary"));reader_dict_ui_close();
    free(base);free(held);s_text=old_text;s_text_len=old_len;s_page=old_page;s_px=old_px;
    assert(relayout());s_dict_restored=false;
    puts("locked-line hold: fixed cue, vertical escape, horizontal targets, full-trail cancel, existing card, wrapped lower segment, cached movement and single release PASS");
}
static void spatial_candidate_test(app_ctx_t *ctx) {
    int old_px=s_px;s_px=40;
    char text[]="quiet reader";
    assert(book_layout_build(text,strlen(text),body_rect(),s_px));
    ui_clear_page(ctx->fb);book_layout_draw_page(ctx->fb,0,body_rect(),s_px);
    dict_geometry_count=0;
    const int left[]={100,112,122,130,142},width[]={10,8,3,8,6};
    for(size_t i=0;i<5;i++)dict_geometry[dict_geometry_count++]=(book_layout_glyph_t){.start=i,.end=i+1,.rect={left[i],100,width[i],40},.px=40};
    for(size_t i=0;i<6;i++)dict_geometry[dict_geometry_count++]=(book_layout_glyph_t){.start=i+6,.end=i+7,.rect={127+(int)i*10,140,8,40},.px=40};
    dict_synthetic_geometry=true;
    glyph_scan_t nearest={.direct=true,.hit=SIZE_MAX,.x=127,.y=138,.bounds=body_rect()};
    assert(dict_test_visit_page(0,body_rect(),s_px,count_glyph,&nearest) && nearest.first==6);
    // 触点在上行整词内部、但处于字母墨迹间隙；下行墨迹更近，仍应优先上行整词。/ A point inside an upper word's letter gap should prefer that word even if lower-line ink is closer.
    unsigned calls=dict_lookup_calls;uint32_t queries=notebook_count("quiet",NULL);
    assert(reader_dict_ui_hold_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,127,138));
    assert(d.holding && d.hold_first==0 && d.hold_end==5 && dict_lookup_calls==calls && notebook_count("quiet",NULL)==queries);
    assert(reader_dict_ui_hold_finish() && !strcmp(d.query,"quiet") && notebook_count("quiet",NULL)==queries+1);
    calls=dict_lookup_calls;
    assert(reader_dict_ui_hold_existing(127,138) && d.hold_first==0 && d.hold_end==5);
    assert(reader_dict_ui_hold_cancel() && d.card);
    assert(reader_dict_ui_hold_existing(128,141) && d.hold_first==6 && d.hold_end==12);
    assert(reader_dict_ui_hold_cancel() && dict_lookup_calls==calls);reader_dict_ui_close();
    // 行中心对齐可胜过略近的邻行墨迹，整词边界的等分仍按阅读顺序决胜。/ Line alignment may beat slightly nearer adjacent-line ink; equal boundary scores retain reading order.
    for(size_t i=0;i<6;i++)dict_geometry[i+5].rect=(EpdRect){149+(int)i*10,124,8,40};
    nearest=(glyph_scan_t){.direct=true,.hit=SIZE_MAX,.x=152,.y=120,.bounds=body_rect()};
    assert(dict_test_visit_page(0,body_rect(),s_px,count_glyph,&nearest) && nearest.first==6);
    assert(reader_dict_ui_hold_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,152,120));
    assert(d.hold_first==0 && d.hold_end==5);reader_dict_ui_close();
    for(size_t i=0;i<6;i++)dict_geometry[i+5].rect=(EpdRect){161+(int)i*10,100,8,40};
    for(int px=154;px<=155;px++) {
        assert(reader_dict_ui_hold_at(text,strlen(text),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,px,120));
        assert(d.hold_first==(px==154?0:6));reader_dict_ui_close();
    }
    // 同词跨行只合并源跨度，空白不能被整个跨行包围框误收。/ Wrapped words share source spans, never one large rectangle that swallows blank space.
    char wrapped[]="quietreader";assert(book_layout_build(wrapped,strlen(wrapped),body_rect(),s_px));
    for(size_t i=0;i<6;i++) {
        dict_geometry[i+5].start=i+5;dict_geometry[i+5].end=i+6;
        dict_geometry[i+5].rect=(EpdRect){200+(int)i*10,220,8,40};
    }
    assert(reader_dict_ui_hold_at(wrapped,strlen(wrapped),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,127,138));
    assert(d.hold_first==0 && d.hold_end==11 && d.n==2);
    cell_t candidate;assert(!ranked_target(170,180,body_rect(),&candidate));
    assert(ranked_target(199,240,body_rect(),&candidate) && candidate.first==0 && candidate.end==11);
    reader_dict_ui_close();
    char punctuated[]="quiet.reader";assert(book_layout_build(punctuated,strlen(punctuated),body_rect(),s_px));
    for(size_t i=0;i<6;i++) { dict_geometry[i+5].start=i+6;dict_geometry[i+5].end=i+7; }
    memmove(dict_geometry+6,dict_geometry+5,6*sizeof(*dict_geometry));
    dict_geometry[5]=(book_layout_glyph_t){.start=5,.end=6,.rect={126,136,2,40},.px=40};dict_geometry_count=12;
    assert(!reader_dict_ui_hold_at(punctuated,strlen(punctuated),0,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H,127,138));
    assert(!reader_dict_ui_active() && dict_lookup_calls==calls && notebook_count("quiet",NULL)==queries+1);
    dict_synthetic_geometry=false;
    s_px=old_px;assert(relayout());s_dict_restored=false;
    puts("spatial candidates: whole-word coverage beats nearer-line ink, line alignment, exact priority, stable ties, wrapped segments, punctuation and no passive queries PASS");
}
static void vocabulary_navigation_test(app_ctx_t *ctx) {
    // 截图使用独立的临时示例，不接触真实阅读记录。/ Preview uses isolated sample records, never real reading history.
    assert(!unlink("/sdcard/notes/vocabulary.jsonl"));
    const char *samples[]={"quiet","山","行","走","extraordinary","一心一意","读","心","心"};
    for(size_t i=0;i<sizeof(samples)/sizeof(*samples);i++)
        assert(reader_vocabulary_record(samples[i],i==0 || i==4?"A Quiet Reader":"西游记",i==4 || i==5 || i==6?READER_DICT_NOT_FOUND:READER_DICT_OK));
    s_view=VIEW_LIBRARY;s_menu=s_info=s_toc=false;s_dict_restored=false;
    render(ctx,ctx->fb);size_t bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *library=malloc(bytes),*list=malloc(bytes);assert(library && list);memcpy(library,ctx->fb,bytes);
    assert(defer_key(ctx,UI_KEY_3));
    s_force_full=false;assert(on_key(ctx,UI_KEY_3)==APP_REDRAW_PAGE && s_force_full);
    assert(!reader_vocabulary_ui_active());s_force_full=false;
    unsigned opens=notebook_opens,closes=notebook_closes;
    assert(on_key_long(ctx,UI_KEY_3)==APP_REDRAW_PAGE && reader_vocabulary_ui_active());
    assert(notebook_opens==opens+1 && notebook_closes==closes+1 && !notebook_session_live);
    opens=notebook_opens;
    assert(reader_surface()==SURFACE_VOCABULARY && !defer_key(ctx,UI_KEY_3));
    unsigned lookups=dict_lookup_calls;
    strcpy(dict_draw_text,"");dict_capture=true;render(ctx,ctx->fb);dict_capture=false;
    assert(dict_lookup_calls==lookups && notebook.count==5);
    assert(notebook.rows[2].result==READER_DICT_NOT_FOUND && strstr(notebook.meanings[2][0],"心思专一"));
    assert(strstr(dict_draw_text,"心脏；心思") && strstr(dict_draw_text,"心思专一") && !strstr(dict_draw_text,"xīn") && strstr(dict_draw_text,"未收录"));
    assert(notebook_word_size && notebook_meaning_size);
    assert(!strstr(dict_draw_text,"次") && !strstr(dict_draw_text,"西游记"));
    memcpy(list,ctx->fb,bytes);dict_preview(ctx,"notebook-inline");
    reader_vocabulary_record_t first;size_t count,total;
    assert(reader_vocabulary_page(0,&first,1,&count,&total) && count==1 && total>7);
    long saved_bytes=notebook_bytes();uint32_t queries=first.count;
    ui_gesture_event_t tap={.type=UI_GESTURE_TAP,.x0=80,.y0=168};
    assert(on_gesture(ctx,&tap)==APP_REDRAW_NONE && !reader_dict_ui_active() && dict_lookup_calls==lookups);
    ui_gesture_event_t hold={.type=UI_GESTURE_LONG_RELEASE,.x0=100,.y0=300};
    assert(on_gesture(ctx,&hold)==APP_REDRAW_NONE);
    assert(notebook_bytes()==saved_bytes && notebook_count(first.query,NULL)==queries);
    ui_gesture_event_t detail={.type=UI_GESTURE_TAP,.x0=UI_LOCK_WIDTH-50,.y0=NB_TOP+40};
    assert(on_gesture(ctx,&detail)==APP_REDRAW_PAGE);
    strcpy(dict_draw_text,"");dict_draw_max_px=0;dict_capture=true;render(ctx,ctx->fb);dict_capture=false;
    assert(strstr(dict_draw_text,"最近阅读") && strstr(dict_draw_text,"西游记") && strstr(dict_draw_text,"2 次") && dict_draw_max_px>=40);
    assert(dict_lookup_calls==lookups && notebook_opens==opens && !notebook_session_live && notebook_bytes()==saved_bytes && notebook_count(first.query,NULL)==queries);
    dict_preview(ctx,"notebook-detail");
    assert(on_gesture(ctx,&tap)==APP_REDRAW_NONE);
    ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_L};
    assert(on_gesture(ctx,&swipe)==APP_REDRAW_NONE);
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && reader_vocabulary_ui_active());
    render(ctx,ctx->fb);assert(!memcmp(ctx->fb,list,bytes) && dict_lookup_calls==lookups);
    assert(on_gesture(ctx,&swipe)==APP_REDRAW_PAGE);assert(dict_lookup_calls==lookups+3);
    render(ctx,ctx->fb);assert(memcmp(ctx->fb,list,bytes));
    assert(strstr(notebook.meanings[2][0],"安静的，平静的") && strstr(notebook.meanings[2][1],"…"));
    for(size_t i=0;i<notebook.count;i++)for(size_t line=0;line<2;line++)
        assert(ttf_text_width_px(32,notebook.meanings[i][line])<=UI_LOCK_WIDTH-2*NB_PAD);
    dict_preview(ctx,"notebook-inline-page2");
    lookups=dict_lookup_calls;opens=notebook_opens;
    assert(on_gesture(ctx,&detail)==APP_REDRAW_PAGE && notebook.page==1);
    assert(on_key(ctx,UI_KEY_3)==APP_REDRAW_FULL && notebook.detail);
    render(ctx,ctx->fb);assert(dict_lookup_calls==lookups && notebook_opens==opens);
    ui_gesture_event_t back={.type=UI_GESTURE_TAP,.x0=UI_LOCK_WIDTH/2,.y0=NB_FOOTER+40};
    assert(on_gesture(ctx,&back)==APP_REDRAW_PAGE && !notebook.detail && notebook.page==1);
    assert(dict_lookup_calls==lookups && notebook_opens==opens);
    swipe.type=UI_GESTURE_SWIPE_R;assert(on_gesture(ctx,&swipe)==APP_REDRAW_PAGE);
    render(ctx,ctx->fb);assert(!memcmp(ctx->fb,list,bytes));
    // 画面只读缓存；翻页重新读当前词库，缺库和内存失败也不改次数。/ Drawing uses cache; page changes use the current dictionary, and missing files or memory never alter counts.
    lookups=dict_lookup_calls;
    assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/notebook-hidden.dict"));
    render(ctx,ctx->fb);assert(dict_lookup_calls==lookups && !memcmp(ctx->fb,list,bytes));
    assert(reader_vocabulary_ui_move(1));
    strcpy(dict_draw_text,"");dict_capture=true;render(ctx,ctx->fb);dict_capture=false;
    assert(strstr(dict_draw_text,"未找到词典"));
    assert(!rename("/sdcard/dict/notebook-hidden.dict","/sdcard/dict/offline.dict"));
    assert(reader_vocabulary_ui_move(-1));render(ctx,ctx->fb);assert(!memcmp(ctx->fb,list,bytes));
    dict_alloc_remaining=0;lookups=dict_lookup_calls;reader_vocabulary_ui_open();
    strcpy(dict_draw_text,"");dict_capture=true;render(ctx,ctx->fb);dict_capture=false;
    assert(dict_lookup_calls==lookups && strstr(dict_draw_text,"暂时无法读取释义"));dict_alloc_remaining=SIZE_MAX;
    reader_vocabulary_ui_open();render(ctx,ctx->fb);assert(!memcmp(ctx->fb,list,bytes));
    notebook_fail_close=true;reader_vocabulary_ui_open();notebook_fail_close=false;
    assert(!notebook_session_live && notebook.readable && notebook.count==5);
    for(size_t i=0;i<notebook.count;i++)assert(!strcmp(notebook.detail_meanings[i],"无法读取词典"));
    opens=notebook_opens;
    lookups=dict_lookup_calls;assert(reader_usb_start()==ESP_OK);reader_vocabulary_ui_open();
    assert(!notebook.readable && dict_lookup_calls==lookups && notebook_opens==opens);assert(reader_usb_stop()==ESP_OK);
    reader_vocabulary_ui_open();render(ctx,ctx->fb);assert(!memcmp(ctx->fb,list,bytes));
    assert(notebook_count(first.query,NULL)==queries);
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && !reader_vocabulary_ui_active());
    render(ctx,ctx->fb);assert(!memcmp(ctx->fb,library,bytes));
    assert(notebook_bytes()==saved_bytes);free(library);free(list);
    if(access("/sdcard/dict/candidate.dict",F_OK)==0) {
        assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/notebook-fixture.dict"));
        assert(!rename("/sdcard/dict/candidate.dict","/sdcard/dict/offline.dict"));
        reader_vocabulary_ui_open();render(ctx,ctx->fb);dict_preview(ctx,"notebook-inline-current-dictionary");
        assert(strstr(notebook.meanings[1][0],"未收录")==NULL);
        assert(on_gesture(ctx,&detail)==APP_REDRAW_PAGE);dict_preview(ctx,"notebook-detail-current-dictionary");
        snprintf(notebook.rows[0].book,sizeof(notebook.rows[0].book),"西游记·齐天大圣与取经路上的故事：写给喜欢阅读与探索的小纸读者");
        dict_preview(ctx,"notebook-detail-long-title");
        reader_vocabulary_ui_close();
        assert(!rename("/sdcard/dict/offline.dict","/sdcard/dict/candidate.dict"));
        assert(!rename("/sdcard/dict/notebook-fixture.dict","/sdcard/dict/offline.dict"));
        assert(notebook_bytes()==saved_bytes);
    }
    // 锁屏/切页的退出与丢卡都清理详情；没有跨事件词库句柄。/ Lock/page exit and media loss clear details; no dictionary handle survives events.
    reader_vocabulary_ui_open();assert(on_gesture(ctx,&detail)==APP_REDRAW_PAGE);
    lookups=dict_lookup_calls;reader_on_exit(ctx);
    assert(!reader_vocabulary_ui_active() && !notebook.detail && !notebook_session_live && dict_lookup_calls==lookups);
    book_layout_set_image_placeholders(false);
    reader_vocabulary_ui_open();assert(on_gesture(ctx,&detail)==APP_REDRAW_PAGE);
    lookups=dict_lookup_calls;on_media_lost(ctx);
    assert(!reader_vocabulary_ui_active() && !notebook.detail && !notebook_session_live && dict_lookup_calls==lookups);
    assert(book_open("/sdcard/books/toc.epub")==ESP_OK);
    strcpy(s_path,"/sdcard/books/toc.epub");strcpy(s_book_title,"目录跳转测试");
    assert(load_text_chapter(0,0,false,1));
    assert(notebook_bytes()==saved_bytes && notebook_count(first.query,NULL)==queries);
    s_view=VIEW_READING;s_dict_restored=false;
    puts("notebook: hidden entry, short refresh, exact counts/provenance, paging, larger inline definitions, cached details/back, one page session, failed close, exit/media cleanup and unchanged library pixels PASS");
}
extern void sim_sd_set_present(bool present);
static void manual_navigation_test(app_ctx_t *ctx) {
    const char *path="/flash/manual/pico-guide.epub",*hidden="/flash/manual/pico-guide.hidden";
    // Metalio 绘制忽略说明书开关，保持原面板；两行命中区域互不重叠。/ Metalio ignores the manual option and keeps its old panel; reader action rows never overlap.
    size_t bytes=(size_t)epd_width()*epd_height()/2;uint8_t *metalio=malloc(bytes);assert(metalio);
    ui_clear_page(ctx->fb);ui_device_panel_draw(ctx->fb,&s_battery,true,UI_DEVICE_USB_OFF,false);memcpy(metalio,ctx->fb,bytes);
    ui_device_panel_draw(ctx->fb,&s_battery,true,UI_DEVICE_USB_OFF,true);assert(!memcmp(metalio,ctx->fb,bytes));free(metalio);
    assert(ui_device_manual_hit(40,324) && ui_device_manual_hit(640,324) && !ui_device_manual_hit(200,368));
    assert(!ui_device_usb_hit(580,268,true) && ui_device_usb_hit(580,412,true));
    sim_sd_set_present(false);s_resume_pending=false;s_view=VIEW_LIBRARY;s_info=s_menu=s_toc=false;
    library_scan();assert(s_card_missing && s_book_n==0);
    ui_gesture_event_t pull={.type=UI_GESTURE_SWIPE_D,.x0=342,.y0=48};
    ui_gesture_event_t manual={.type=UI_GESTURE_TAP,.x0=200,.y0=324};
    assert(on_gesture(ctx,&pull)==APP_REDRAW_PAGE && s_info);
    dict_preview(ctx,"manual-device-panel");ctx->request_app=NULL;
    assert(on_gesture(ctx,&manual)==APP_REDRAW_PAGE && s_view==VIEW_READING && !s_info);
    assert(!strcmp(s_path,path) && !strcmp(s_book_title,"使用说明") && !ctx->request_app && s_book_n==0);
    dict_preview(ctx,"manual-reading");open_toc();assert(s_toc && book_toc_count()>1);dict_preview(ctx,"manual-toc");
    assert(toc_jump(ctx,1)==APP_REDRAW_PAGE && !s_toc);
    size_t chapter=s_chapter,page=s_page,offset=book_layout_page_start_offset(s_page);
    reader_on_exit(ctx);on_enter(ctx);assert(s_view==VIEW_READING && s_chapter==chapter && s_page==page);
    char last[PATH_BYTES];assert(book_progress_last_path(last,sizeof(last)) && !strcmp(last,path));
    free_book();s_view=VIEW_LIBRARY;s_resume_pending=true;library_scan();
    assert(resume_book(ctx)==APP_REDRAW_PAGE && s_view==VIEW_READING && !s_resume_pending);
    assert(!strcmp(s_path,path) && s_chapter==chapter && book_layout_page_start_offset(s_page)==offset);
    char *text=s_text;on_media_lost(ctx);assert(s_view==VIEW_READING && s_text==text && !strcmp(s_path,path));
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_view==VIEW_LIBRARY && s_book_n==0);
    assert(!book_progress_last_path(last,sizeof(last)) || !*last);
    assert(!rename(path,hidden));
    assert(on_gesture(ctx,&pull)==APP_REDRAW_PAGE && s_info);
    assert(on_gesture(ctx,&manual)==APP_REDRAW_PAGE && s_view==VIEW_LIBRARY && !s_info);
    assert(strstr(s_notice,"使用说明") && !ctx->request_app);
    render(ctx,ctx->fb);uint8_t *notice_pixels=malloc(bytes);assert(notice_pixels);memcpy(notice_pixels,ctx->fb,bytes);
    char notice[sizeof(s_notice)];strcpy(notice,s_notice);s_notice[0]=0;render(ctx,ctx->fb);
    assert(memcmp(notice_pixels,ctx->fb,bytes));free(notice_pixels);strcpy(s_notice,notice);
    dict_preview(ctx,"manual-missing");
    FILE *bad=fopen(path,"wb");assert(bad && fputs("invalid epub",bad)>=0 && !fclose(bad));
    assert(on_gesture(ctx,&pull)==APP_REDRAW_PAGE && s_info);
    assert(on_gesture(ctx,&manual)==APP_REDRAW_PAGE && s_view==VIEW_LIBRARY && !s_info);
    assert(strstr(s_notice,"使用说明") && !ctx->request_app);
    assert(!unlink(path) && !rename(hidden,path));sim_sd_set_present(true);library_scan();
    for(int i=0;i<s_book_n;i++)assert(strcmp(s_books[i].name,"pico-guide.epub"));
    assert(s_book_n==1 && open_book(ctx,0,false)==APP_REDRAW_PAGE && s_view==VIEW_READING);
    assert(!strcmp(s_path,"/sdcard/books/toc.epub") && strcmp(s_book_title,"使用说明"));
    reader_on_exit(ctx);free_book();s_view=VIEW_LIBRARY;s_resume_pending=true;library_scan();
    assert(resume_book(ctx)==APP_REDRAW_PAGE && !strcmp(s_path,"/sdcard/books/toc.epub"));
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_view==VIEW_LIBRARY);
    puts("manual: hidden from library, no-card open, TOC, warm/cold resume, media loss, return and missing/corrupt errors PASS");
}
static void battery_solid_test(uint8_t* fb) {
    size_t bytes=684u*1216u/2u;
    uint8_t* mask=malloc(bytes);assert(mask);
    ui_battery_t b={.valid=true,.charging=true,.percent=80};
    ui_clear_page(fb);ui_battery_draw(fb,10,10,&b);
    memset(mask,0xff,bytes);
    epd_fill_rect((EpdRect){38,22,30,8},UI_GRAY_BLACK,mask);
    for(size_t i=0;i<bytes;i++)assert((fb[i] & (uint8_t)~mask[i])==(0x88u & (uint8_t)~mask[i]));
    free(mask);
}
int main(void) {
    epd_init(&epd_board_read_pico,&E0470_DISPLAY,EPD_LUT_1K);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    app_settings_init();assert(ttf_font_init()==ESP_OK);assert(ttf_font_open("/flash/fonts/Fallback.ttf")==ESP_OK);
    EpdiyHighlevelState hl=epd_hl_init(&READER_WAVEFORM);
    app_ctx_t ctx={.hl=&hl,.fb=epd_hl_get_framebuffer(&hl),.now_ms=10000};
    battery_solid_test(ctx.fb);
    s_soft_refreshes=0;
    memset(ctx.fb,0x78,684u*1216u/2u);
    assert(update_display_reader_fast(&hl)==0);
    for(size_t i=0;i<684u*1216u/2u;i++)assert(ctx.fb[i]==0x78 && hl.back_fb[i]==0x78);
    memset(ctx.fb,0x87,684u*1216u/2u);
    assert(update_display_reader_fast(&hl)==0);
    assert(ctx.fb[0]==0x87 && hl.back_fb[0]==0x87);
    s_soft_refreshes=APP_GC16_EVERY-1;
    memset(ctx.fb,0x78,684u*1216u/2u);
    assert(update_display_reader_fast(&hl)==0);
    assert(ctx.fb[0]==0x78 && hl.back_fb[0]==0x78 && s_soft_refreshes==0);
    s_soft_refreshes=0;
    memset(ctx.fb,0xff,684u*1216u/2u);
    for(unsigned i=0;i<256;i++)ctx.fb[i]=(uint8_t)i;
    assert(update_display_reader_fast(&hl)==0);
    for(unsigned i=0;i<256;i++)assert(ctx.fb[i]==i && hl.back_fb[i]==ctx.fb[i]);
    s_soft_refreshes=7;
    assert(update_display_area_with(&hl,&READER_WAVEFORM,MODE_GC16,(EpdRect){0,0,32,32})==0);
    assert(s_soft_refreshes==7);
    assert(book_open("/sdcard/books/toc.epub")==ESP_OK);
    strcpy(s_path,"/sdcard/books/toc.epub");strcpy(s_book_title,"目录跳转测试");
    assert(load_text_chapter(0,0,false,1));s_view=VIEW_READING;s_resume_pending=false;
    dictionary_test(&ctx);
    small_hold_geometry_test(&ctx);
    locked_line_hold_test(&ctx);
    spatial_candidate_test(&ctx);
    vocabulary_navigation_test(&ctx);
    battery=(pmu_snapshot_t){.present=true,.status_ok=true,.flags=PMU_STATUS_BATTERY_VALID,.soc_permille=800,.last_err=ESP_OK};
    battery.flags=0;reader_battery_sample(&s_battery);assert(!s_battery.valid);
    battery.flags=PMU_STATUS_BATTERY_VALID;reader_battery_sample(&s_battery);assert(s_battery.valid);
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE && s_menu);
    menu_geom_t g;menu_geometry(&g);
    assert(g.panel.y>=0);
    preview(&ctx,"settings");
    ui_gesture_event_t pull={.type=UI_GESTURE_SWIPE_D,.x0=342,.x=342,.y0=48,.y=200};
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_NONE && s_menu && !s_toc);
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE && !s_menu);
    pull.y0=200;pull.y=400;
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_NONE && !s_toc);
    pull.y0=48;pull.y=200;
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_PAGE && s_toc && !s_menu);
    assert(book_toc_count()==14);assert(s_toc_current==0);preview(&ctx,"toc");
    assert(toc_move(-1)==APP_REDRAW_NONE);
    // 目录左右页脚不再是按钮；只保留中间返回。/ Footer sides are inert; only center returns.
    ui_gesture_event_t empty_toc_footer={.type=UI_GESTURE_TAP,.x0=600,.y0=TOC_FOOT_Y+24};
    assert(toc_gesture(&ctx,&empty_toc_footer)==APP_REDRAW_NONE && s_toc_page==0 && s_toc);
    assert(toc_move(1)==APP_REDRAW_PAGE && s_toc_page==1);
    assert(toc_move(1)==APP_REDRAW_NONE);
    preview(&ctx,"toc-second");
    assert(toc_move(-1)==APP_REDRAW_PAGE && s_toc_page==0);
    size_t chapter=s_chapter,page=s_page;
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && !s_toc && s_view==VIEW_READING);
    assert(s_chapter==chapter && s_page==page);
    open_toc();assert(s_toc);
    assert(toc_jump(&ctx,1)==APP_REDRAW_PAGE && !s_toc && s_page>0);
    chapter=s_chapter;page=s_page;
    open_toc();assert(s_toc_current==1 && s_toc_page==0);preview(&ctx,"toc-current");
    assert(toc_jump(&ctx,2)==APP_REDRAW_NONE && s_toc);
    assert(s_chapter==chapter && s_page==page);
    assert(toc_jump(&ctx,13)==APP_REDRAW_NONE && s_toc);
    assert(s_chapter==chapter && s_page==page);
    assert(toc_jump(&ctx,12)==APP_REDRAW_PAGE);
    open_toc();assert(s_toc_current==12 && s_toc_page==1);preview(&ctx,"toc-current-page2");
    s_toc=false;s_toast_until_ms=0;
    uint8_t* healthy=malloc(684u*1216u/2u);assert(healthy);
    render(&ctx,ctx.fb);memcpy(healthy,ctx.fb,684u*1216u/2u);
    ui_battery_update(&s_battery,false,0,false);render(&ctx,ctx.fb);
    assert(!memcmp(healthy,ctx.fb,684u*1216u/2u));
    ui_battery_update(&s_battery,true,150,false);render(&ctx,ctx.fb);
    assert(memcmp(healthy,ctx.fb,684u*1216u/2u));preview(&ctx,"reading-low");
    s_presented_surface=SURFACE_BODY;s_force_full=false;
    assert(present(&ctx,APP_REDRAW_PAGE));
    assert(!memcmp(ctx.fb,hl.back_fb,684u*1216u/2u));
    unsigned reads=battery_reads;render(&ctx,ctx.fb);assert(battery_reads==reads);
    ui_battery_update(&s_battery,true,800,false);preview(&ctx,"reading-normal");
    ui_battery_update(&s_battery,true,800,true);s_menu=true;preview(&ctx,"settings-charging");s_menu=false;
    s_view=VIEW_LIBRARY;s_book_n=0;s_resume_pending=false;preview(&ctx,"library-empty");
    s_books=calloc(LIB_MAX,sizeof(*s_books));assert(s_books);
    s_book_n=1;s_books[0].meta_ready=true;strcpy(s_books[0].title,"目录跳转测试");strcpy(s_books[0].name,"toc.epub");
    const char* sample_titles[]={"阅读与生活","文字的故事","山川与河流","语言的边界","书页之间","漫长的旅程"};
    for(int i=1;i<7;i++){s_books[i].meta_ready=true;strcpy(s_books[i].title,sample_titles[i-1]);snprintf(s_books[i].name,sizeof(s_books[i].name),"sample%d.epub",i);}
    s_book_n=7;
    pages_rebuild(0);preview(&ctx,"library");
    render(&ctx,ctx.fb);memcpy(healthy,ctx.fb,684u*1216u/2u);
    ui_battery_update(&s_battery,true,150,false);render(&ctx,ctx.fb);
    assert(!memcmp(healthy,ctx.fb,684u*1216u/2u));
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_PAGE && s_info && !s_menu);
    preview(&ctx,"device-info");
    // 下拉只覆盖顶部，书库下部像素保持。/ The drawer covers only the top; the library remains below it.
    render(&ctx,ctx.fb);
    uint8_t* lower_mask=malloc(684u*1216u/2u);assert(lower_mask);
    memset(lower_mask,0xff,684u*1216u/2u);
    epd_fill_rect((EpdRect){0,UI_DEVICE_READER_PANEL_HEIGHT,684,1216-UI_DEVICE_READER_PANEL_HEIGHT},0,lower_mask);
    for(size_t i=0;i<684u*1216u/2u;i++)assert((ctx.fb[i] & ~lower_mask[i])==(healthy[i] & ~lower_mask[i]));
    free(lower_mask);
    ui_gesture_event_t outside={.type=UI_GESTURE_TAP,.x0=342,.y0=UI_DEVICE_READER_PANEL_HEIGHT+20};
    assert(on_gesture(&ctx,&outside)==APP_REDRAW_PAGE && !s_info && s_view==VIEW_LIBRARY);
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_PAGE && s_info);
    ui_gesture_event_t usb_tap={.type=UI_GESTURE_TAP,.x0=580,.y0=280+UI_DEVICE_MANUAL_OFFSET};
    ctx.request_app=NULL;
    assert(on_gesture(&ctx,&usb_tap)==APP_REDRAW_NONE && ctx.request_app!=NULL);
    assert(strcmp(ctx.request_app->title,"USB 传书")==0);
    ctx.request_app=NULL;

    ui_gesture_event_t wifi_tap={.type=UI_GESTURE_TAP,.x0=580,.y0=500};
    assert(ui_device_wifi_hit(580,500) && !ui_device_usb_hit(580,500,true));
    assert(on_gesture(&ctx,&wifi_tap)==APP_REDRAW_NONE && ctx.request_app==&app_wifi_transfer);
    ctx.request_app=NULL;
    reader_clock_init();
    ui_gesture_event_t clock_tap={.type=UI_GESTURE_TAP,.x0=120,.y0=588};
    assert(ui_device_clock_hit(120,588) && !ui_device_wifi_hit(120,588));
    assert(on_gesture(&ctx,&clock_tap)==APP_REDRAW_PAGE && !ctx.request_app && s_info);
    assert(s_clock_status.state==READER_CLOCK_NO_WIFI || s_clock_status.state==READER_CLOCK_ERROR || s_clock_status.state==READER_CLOCK_BUSY);
    s_clock_next_app=&app_wifi_transfer;
    int library_page=s_lib_page;
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && !s_info && s_lib_page==library_page);
    assert(!s_mid_long_armed && !s_clock_next_app);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_NONE);
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_NONE && !ctx.request_menu);
    assert(on_gesture(&ctx,&pull)==APP_REDRAW_PAGE && s_info);
    ui_gesture_event_t up={.type=UI_GESTURE_SWIPE_U,.x0=342,.y0=800,.x=342,.y=600};
    assert(on_gesture(&ctx,&up)==APP_REDRAW_PAGE && !s_info);
    free(healthy);free_book();free(s_books);s_books=NULL;
    manual_navigation_test(&ctx);
    assert(ttf_font_open("/sdcard/fonts/Saved.ttf")==ESP_OK);
    assert(ttf_font_on_card());
    app_settings_set_font_path("/sdcard/fonts/Saved.ttf");
    usb_enter(&ctx);
    assert(!ttf_font_on_card() && usb_pending && !fake_usb_active);
    assert(app_usb_transfer.blocks_idle_sleep && app_usb_transfer.blocks_power_lock && app_usb_transfer.blocks_media_access);
    assert(usb_tick(&ctx)==APP_REDRAW_PAGE && fake_usb_active);
    assert(!app_usb_transfer.enter_full && app_usb_transfer.present);
    s_soft_refreshes=APP_GC16_EVERY-1;
    assert(usb_present(&ctx,APP_REDRAW_PAGE) && s_soft_refreshes==APP_GC16_EVERY-1);
    fake_usb_state=READER_USB_CONNECTED;assert(usb_tick(&ctx)==APP_REDRAW_PAGE);
    assert(usb_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE);
    usb_render(&ctx,ctx.fb);usb_preview(&ctx,"usb-connected");
    ui_gesture_event_t no_manual={.type=UI_GESTURE_TAP,.x0=200,.y0=324};
    assert(usb_gesture(&ctx,&no_manual)==APP_REDRAW_NONE && !ctx.request_app && fake_usb_active);
    assert(!ui_device_usb_hit(410,280,false)); // 状态文字不可当结束按钮。/ Status is not the End action.
    ui_gesture_event_t status_tap={.type=UI_GESTURE_TAP,.x0=410,.y0=280};
    assert(usb_gesture(&ctx,&status_tap)==APP_REDRAW_NONE && !plain_stops && !confirmed_stops);
    ui_gesture_event_t off={.type=UI_GESTURE_TAP,.x0=570,.y0=280};
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_PAGE && usb_confirm && fake_usb_active && !usb_reboots);
    assert(plain_stops==1 && !confirmed_stops);usb_preview(&ctx,"usb-exit");
    ui_gesture_event_t cancel={.type=UI_GESTURE_TAP,.x0=180,.y0=684};
    assert(usb_gesture(&ctx,&cancel)==APP_REDRAW_PAGE && !usb_confirm && fake_usb_active);
    fake_usb_state=READER_USB_EJECTED;usb_tick(&ctx);
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_PAGE && usb_returning && !fake_usb_active);
    usb_preview(&ctx,"usb-ending");
    assert(usb_reboots==0 && !confirmed_stops && usb_probes==0 && ctx.request_app==NULL);
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && usb_probes==1 && ctx.request_app==NULL && !ttf_font_on_card());
    ctx.now_ms+=8100;
    assert(usb_tick(&ctx)==APP_REDRAW_PAGE && usb_probe_slow && !ctx.request_app);
    usb_fake_probing=false;ctx.now_ms+=200;
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && ctx.request_app==&app_reader && ttf_font_on_card());
    usb_leave(&ctx);ctx.request_app=NULL;
    usb_enter(&ctx);usb_tick(&ctx);fake_usb_state=READER_USB_CONNECTED;usb_tick(&ctx);
    usb_gesture(&ctx,&off);assert(usb_confirm);
    ui_gesture_event_t confirm={.type=UI_GESTURE_TAP,.x0=490,.y0=684};
    fake_usb_stop_error=ESP_FAIL;
    assert(usb_gesture(&ctx,&confirm)==APP_REDRAW_PAGE && fake_usb_active && !usb_reboots && !usb_confirm);
    fake_usb_stop_error=ESP_OK;usb_gesture(&ctx,&off);
    assert(usb_gesture(&ctx,&confirm)==APP_REDRAW_PAGE && usb_returning && !fake_usb_active);
    usb_fake_missing=true;
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && !ctx.request_app);ctx.now_ms+=200;
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && !ctx.request_app);ctx.now_ms+=200;
    usb_fake_probing=false;
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && ctx.request_app==&app_reader && !ttf_font_on_card());
    assert(confirmed_stops==2 && !usb_reboots && usb_probes==2);usb_leave(&ctx);
    fake_usb_start_error=ESP_ERR_NOT_FOUND;usb_enter(&ctx);usb_tick(&ctx);ctx.request_app=NULL;
    assert(!fake_usb_active && !usb_pending && !usb_returning);
    usb_preview(&ctx,"usb-inactive");
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_PAGE && usb_pending && !usb_returning && !ctx.request_app);
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_NONE); // 准备中不可重复开启。/ Ignore repeated Start while preparing.
    fake_usb_start_error=ESP_OK;
    assert(usb_tick(&ctx)==APP_REDRAW_PAGE && fake_usb_active && !usb_returning);
    fake_usb_state=READER_USB_EJECTED;usb_tick(&ctx);
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_PAGE && usb_returning && !ctx.request_app && !usb_reboots);
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && !ctx.request_app);ctx.now_ms+=200;
    usb_fake_probing=false;usb_tick(&ctx);assert(ctx.request_app==&app_reader && usb_probes==3);
    usb_leave(&ctx);ctx.request_app=NULL;
    fake_usb_start_error=ESP_OK;usb_enter(&ctx);usb_tick(&ctx);
    fake_usb_state=READER_USB_EJECTED;fake_usb_stop_error=ESP_FAIL;usb_fake_restart=true;
    assert(usb_gesture(&ctx,&off)==APP_REDRAW_PAGE && usb_restart_needed && !usb_confirm);
    assert(usb_key(&ctx,UI_KEY_2)==APP_REDRAW_NONE && !usb_reboots && !ctx.request_app);
    assert(usb_tick(&ctx)==APP_REDRAW_NONE && usb_probes==3);
    if(!setjmp(usb_restart_done)){usb_gesture(&ctx,&confirm);assert(!"explicit fallback restart expected");}
    assert(usb_reboots==1);
    puts("reader: TOC, compact info drawer, low battery and USB lifecycle PASS");
}
