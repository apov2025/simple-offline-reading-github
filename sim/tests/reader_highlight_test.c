/* SPDX-License-Identifier: Apache-2.0
 * 真实阅读器、排版、字体和临时卡验证整句划线。/ Test sentence highlights with the real reader, layout, font and a temporary card.
 */
#include "../../main/apps/app_reader.c"
#include "read_pico_board.h"
#include "annotations/reader_highlight_store.h"
#include "annotations/reader_sentence.h"
#include "board/reader_screenshot.h"
#include <assert.h>

static size_t geometry_visits,load_calls,save_calls,sentence_scans;
static bool counted_visit(size_t page,EpdRect body,int px,book_layout_glyph_visitor_t visitor,void *ctx) {
    ++geometry_visits;return book_layout_visit_page(page,body,px,visitor,ctx);
}
static bool counted_load(const char *path,uint32_t bytes,size_t chapter,const char *text,size_t len,
                         reader_highlight_range_t *ranges,size_t capacity,size_t *count) {
    ++load_calls;return reader_highlight_load(path,bytes,chapter,text,len,ranges,capacity,count);
}
static bool counted_save_at(const char *path,const char *title,uint32_t bytes,size_t chapter,const char *chapter_title,
                         const char *text,size_t len,size_t start,size_t end,int64_t epoch) {
    ++save_calls;return reader_highlight_save_at(path,title,bytes,chapter,chapter_title,text,len,start,end,epoch);
}
static bool counted_sentence_span(const char *text,size_t len,size_t first,size_t last,size_t at,size_t *start,size_t *end) {
    ++sentence_scans;return reader_sentence_span(text,len,first,last,at,start,end);
}
#define reader_sentence_span counted_sentence_span
#define book_layout_visit_page counted_visit
#define reader_highlight_load counted_load
#define reader_highlight_save_at counted_save_at
#include "../../main/annotations/reader_highlight_ui.c"
#undef reader_sentence_span
#undef book_layout_visit_page
#undef reader_highlight_load
#undef reader_highlight_save_at

extern void sim_sd_set_present(bool present);
static void preview(app_ctx_t *ctx,const char *name) {
    const char *folder=getenv("HIGHLIGHT_PREVIEW");if(!folder)return;
    char path[512];render(ctx,ctx->fb);assert(reader_screenshot_write(folder,name,ctx->fb,1216,684,path,sizeof(path)));
}
static void fixture(app_ctx_t *ctx,const char *name,const char *text,int px) {
    free_book();s_text=strdup(text);assert(s_text);s_text_len=strlen(text);s_px=px;s_chapter=0;s_page=0;
    snprintf(s_path,sizeof(s_path),"/sdcard/books/%s.epub",name);strcpy(s_book_title,"划线测试");strcpy(s_chapter_title,"第一章");s_file_size=123;
    assert(layout_text(s_text,s_text_len,NULL,0,0,false));s_view=VIEW_READING;s_resume_pending=false;
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,s_chapter,s_chapter_title,s_text,s_text_len);assert(h.loaded);render(ctx,ctx->fb);
}
static void open_selection(app_ctx_t *ctx) {
    render(ctx,ctx->fb);assert(reader_highlight_ui_open(s_page,body_rect(),s_px,ctx->fb,UI_LOCK_HEIGHT-READ_FOOTER_H));
    assert(h.active && !h.selected && !reader_dict_ui_active());
}
static void tap_offset(app_ctx_t *ctx,size_t offset) {
    for(size_t i=0;i<h.n;i++)if(h.cells[i].first==offset) {
        EpdRect r=h.cells[i].rect;ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=r.x+r.width/2,.y0=r.y+r.height/2};
        size_t loads=load_calls,saves=save_calls,page=s_page;assert(on_gesture(ctx,&ev)==APP_REDRAW_PAGE);
        assert(load_calls==loads && save_calls==saves && s_page==page);return;
    }
    assert(!"tap target must be present in the visible page");
}
static void tap_text(app_ctx_t *ctx,const char *needle) {
    const char *found=strstr(s_text,needle);assert(found);tap_offset(ctx,(size_t)(found-s_text));
}
static void save_selection(app_ctx_t *ctx) {
    ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=600,.y0=1180};assert(on_gesture(ctx,&ev)==APP_REDRAW_PAGE);
}
static void assert_stored(size_t start,size_t end) {
    reader_highlight_range_t ranges[4];size_t count=0;
    assert(reader_highlight_load(s_path,s_file_size,s_chapter,s_text,s_text_len,ranges,4,&count));
    assert(count==1 && ranges[0].start==start && ranges[0].end==end);
}
static void assert_marks_on_page(app_ctx_t *ctx,size_t page) {
    const size_t bytes=(size_t)epd_width()*epd_height()/2;uint8_t *plain=malloc(bytes);assert(plain);
    memset(ctx->fb,0xff,bytes);book_layout_draw_page(ctx->fb,page,body_rect(),s_px);memcpy(plain,ctx->fb,bytes);
    size_t loads=load_calls,saves=save_calls;geometry_visits=0;
    reader_highlight_ui_marks(ctx->fb,page,body_rect(),s_px);
    assert(geometry_visits==1 && load_calls==loads && save_calls==saves && memcmp(plain,ctx->fb,bytes));
    free(plain);
}
static unsigned pixel(const uint8_t *fb,int x,int y) {
    EpdRect p=ui_rotate_rect_to_fb((EpdRect){x,y,1,1});
    size_t off=(size_t)p.y*epd_width()/2+(size_t)p.x/2;
    return (fb[off]>>((p.x&1)*4))&15;
}
static EpdRect sentence_row(size_t first,size_t last,int row_y) {
    EpdRect r={0};
    for(size_t i=0;i<h.n;i++)if(h.cells[i].first>=first && h.cells[i].end<=last && h.cells[i].rect.y==row_y)
        r=r.width?ui_rect_union(r,h.cells[i].rect):h.cells[i].rect;
    assert(r.width>0);return r;
}
static size_t changed_row(const uint8_t *before,const uint8_t *after,EpdRect r,int y) {
    size_t changed=0;
    for(int x=r.x;x<r.x+r.width;x++)changed+=pixel(before,x,y)!=pixel(after,x,y);
    return changed;
}
static bool du_drives_black(unsigned gray) {
    const EpdWaveformPhases *du=NULL;
    for(int i=0;i<E0470_WAVEFORM.num_modes;i++)if(E0470_WAVEFORM.mode_data[i]->type==MODE_DU)
        du=E0470_WAVEFORM.mode_data[i]->range_data[0];
    assert(du);bool pushes=false;
    for(int phase=0;phase<du->phases;phase++) {
        int black=e0470_phase_action(du,phase,0,15);
        if(e0470_phase_action(du,phase,(int)gray,15)!=black)return false;
        pushes|=black==1;
    }
    return pushes;
}
static void test_sentence_boxes(app_ctx_t *ctx) {
    const char *text="First. Next.\n\nThis is a longer sentence that wraps over several lines and keeps each visible line outlined separately so the surrounding text does not move.\n\n最后一段跨过空行也能选中。";
    fixture(ctx,"sentence-boxes",text,40);preview(ctx,"sentences-before");
    size_t bytes=(size_t)epd_width()*epd_height()/2;uint8_t *plain=malloc(bytes),*idle=malloc(bytes);assert(plain && idle);
    memcpy(plain,ctx->fb,bytes);open_selection(ctx);
    size_t loads=load_calls,saves=save_calls,scans=sentence_scans,visits=geometry_visits;
    render(ctx,ctx->fb);memcpy(idle,ctx->fb,bytes);
    assert(load_calls==loads && save_calls==saves && sentence_scans==scans && geometry_visits==visits);
    const size_t first_end=strlen("First."),next_start=strlen("First. "),next_end=strlen("First. Next.");
    EpdRect first=sentence_row(0,first_end,h.cells[0].rect.y);
    EpdRect next=sentence_row(next_start,next_end,h.cells[0].rect.y);
    int top=first.y-4;if(top<h.body.y)top=h.body.y;
    // 入模式时两句均已有上/下/侧边框，而不是只有页脚变化。
    // Both sentences have top/bottom/side borders on entry, not just a changed footer.
    assert(changed_row(plain,idle,first,top)>(size_t)first.width/2);
    assert(changed_row(plain,idle,next,top)>(size_t)next.width/2);
    size_t side=0;for(int y=first.y;y<first.y+first.height;y++)side+=pixel(plain,first.x,y)!=pixel(idle,first.x,y);
    assert(side>(size_t)first.height/3);
    for(int x=first.x+first.width;x<next.x;x++)assert(pixel(idle,x,top)==pixel(plain,x,top));
    // 跨行句每行都有独立框，不把旁边的下一句圈进去。
    // A wrapped sentence has a separate outline on every line, without enclosing its neighbor.
    size_t long_start=(size_t)(strstr(text,"This")-text),long_end=(size_t)(strstr(text,"move.")-text)+strlen("move.");
    int previous_y=-1;size_t wrapped_rows=0;
    for(size_t i=0;i<h.n;i++)if(h.cells[i].first>=long_start && h.cells[i].end<=long_end && h.cells[i].rect.y!=previous_y) {
        previous_y=h.cells[i].rect.y;EpdRect row=sentence_row(long_start,long_end,previous_y);
        assert(changed_row(plain,idle,row,row.y-4)>(size_t)row.width/2);wrapped_rows++;
    }
    assert(wrapped_rows>1);preview(ctx,"sentences-outlined");
    size_t scans_before=sentence_scans;tap_text(ctx,"First");render(ctx,ctx->fb);
    assert(sentence_scans==scans_before && load_calls==loads && save_calls==saves);
    size_t darkened=0;for(int x=first.x;x<first.x+first.width;x++)darkened+=pixel(ctx->fb,x,top)<pixel(idle,x,top);
    assert(darkened>(size_t)first.width/2);
    // 选中框至少两行纯黑，边缘空白不再增加句选黑点。/ Selected borders have at least two black rows; sentence selection adds no margin dots.
    size_t strong=0;
    for(int x=first.x+4;x<first.x+first.width-4;x++)
        strong+=pixel(ctx->fb,x,top)==0 && pixel(ctx->fb,x,top+1)==0;
    assert(strong>(size_t)first.width/2);
    for(int y=h.body.y;y<h.footer;y++) {
        for(int x=0;x<h.body.x;x++)assert(pixel(ctx->fb,x,y)==pixel(plain,x,y));
        for(int x=h.body.x+h.body.width;x<UI_LOCK_WIDTH;x++)assert(pixel(ctx->fb,x,y)==pixel(plain,x,y));
    }
    for(int x=next.x;x<next.x+next.width;x++)assert(pixel(ctx->fb,x,top)==pixel(idle,x,top));
    assert(changed_row(plain,ctx->fb,first,first.y+first.height)<=4);
    // 字形深色像素不能被浅框洗白。/ Light borders must never wash out dark glyph pixels.
    for(int y=h.body.y;y<h.body.y+h.body.height;y++)for(int x=h.body.x;x<h.body.x+h.body.width;x++)
        assert(pixel(ctx->fb,x,y)<=pixel(plain,x,y));
    preview(ctx,"sentence-selected-box");save_selection(ctx);assert(!h.active);assert_stored(0,first_end);render(ctx,ctx->fb);
    assert(!changed_row(plain,ctx->fb,first,top));
    assert(changed_row(plain,ctx->fb,first,first.y+first.height-1)>0);
    // 检查真实默认 DU 每相动作，保存线两行均必须有压黑驱动，不能被量化成白。
    // Check every actual default DU phase: both saved stroke rows must drive black instead of quantizing to white.
    size_t stable=0;
    for(int y=first.y+first.height-1;y<=first.y+first.height;y++)for(int x=first.x;x<first.x+first.width;x++) {
        if(pixel(plain,x,y)!=15)continue;
        assert(du_drives_black(pixel(ctx->fb,x,y)));stable++;
    }
    assert(stable>(size_t)first.width);
    assert(!changed_row(plain,ctx->fb,next,top));preview(ctx,"sentence-confirmed-underlined");
    free(plain);free(idle);
}
static void test_paragraph_neighbors(app_ctx_t *ctx) {
    const char *separators[]={"\n","\n\n","\r\n\r\n","\n　","\n\xc2\xa0","\n\xe2\x80\xaf"};
    for(size_t k=0;k<sizeof(separators)/sizeof(separators[0]);k++) {
        char name[64],text[512];snprintf(name,sizeof(name),"paragraphs-%zu",k);
        snprintf(text,sizeof(text),"第一句。%sSecond sentence!%s第三句？",separators[k],separators[k]);
        fixture(ctx,name,text,40);open_selection(ctx);tap_text(ctx,"第一");tap_text(ctx,"Second");
        size_t second=(size_t)(strstr(text,"Second")-text),end=second+strlen("Second sentence!");
        assert(h.first==0 && h.last==end);tap_text(ctx,"第三");assert(h.first==0 && h.last==strlen(text));
        tap_text(ctx,"第三");assert(h.first==0 && h.last==end);save_selection(ctx);assert_stored(0,end);
    }
    const char *text="第一段没有句末标点\n\n　Second paragraph without full stop\n\n第三段结束。";
    fixture(ctx,"paragraphs-no-terminal",text,40);open_selection(ctx);tap_text(ctx,"第一");tap_text(ctx,"Second");
    size_t end=(size_t)(strstr(text,"\n\n第三")-text);assert(h.first==0 && h.last==end);save_selection(ctx);assert_stored(0,end);
    fixture(ctx,"nonadjacent-sentences","First. Middle. Last.",40);open_selection(ctx);tap_text(ctx,"First");tap_text(ctx,"Last");
    assert(h.first==strlen("First. Middle. ") && h.last==strlen(s_text));save_selection(ctx);
    assert_stored(strlen("First. Middle. "),strlen(s_text));
}

static void test_whole_sentence(app_ctx_t *ctx) {
    char text[8192]="前句。\xc2\xa0";const size_t begin=strlen(text);
    for(int i=0;i<180;i++)strcat(text,"山川与河流");strcat(text,"。");const size_t end=strlen(text);strcat(text,"　后句。");
    fixture(ctx,"whole-sentence",text,48);while(!book_layout_complete())assert(book_layout_extend(2));
    assert(book_layout_page_count()>=3);s_page=1;open_selection(ctx);tap_offset(ctx,h.cells[h.n/2].first);
    assert(h.first==begin && h.last==end);assert(h.first<h.start && h.last>h.end);
    preview(ctx,"whole-sentence-visible-part");
    size_t page=s_page;ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_L};
    assert(on_gesture(ctx,&swipe)==APP_REDRAW_NONE && h.active && s_page==page);
    size_t saves_before_cancel=save_calls;ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};assert(on_gesture(ctx,&cancel)==APP_REDRAW_NONE && save_calls==saves_before_cancel);
    save_selection(ctx);assert(!h.active);assert_stored(begin,end);
    render(ctx,ctx->fb);size_t frame_bytes=(size_t)epd_width()*epd_height()/2;
    uint8_t *confirmed=malloc(frame_bytes);assert(confirmed);memcpy(confirmed,ctx->fb,frame_bytes);
    assert(turn_page(1)==APP_REDRAW_PAGE);render(ctx,ctx->fb);
    assert(turn_page(-1)==APP_REDRAW_PAGE);render(ctx,ctx->fb);
    assert(!memcmp(confirmed,ctx->fb,frame_bytes));free(confirmed);
    assert_marks_on_page(ctx,0);assert_marks_on_page(ctx,1);assert_marks_on_page(ctx,book_layout_page_for_offset(end-1));
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,s_chapter,s_chapter_title,s_text,s_text_len);
    assert(h.loaded && h.mark_count==1 && h.marks[0].start==begin && h.marks[0].end==end);
    s_px=36;assert(relayout());while(!book_layout_complete())assert(book_layout_extend(2));
    assert_marks_on_page(ctx,0);assert_marks_on_page(ctx,book_layout_page_for_offset(end-1));assert_stored(begin,end);
    preview(ctx,"whole-sentence-reflow");
}
static void test_unicode_neighbors(app_ctx_t *ctx) {
    const char *text="First.\xc2\xa0Second.\xe2\x80\x89Third.";
    const size_t second=strlen("First.\xc2\xa0"),second_end=strlen("First.\xc2\xa0Second.");
    fixture(ctx,"unicode-neighbors",text,48);open_selection(ctx);
    tap_text(ctx,"First");tap_text(ctx,"Second");assert(h.first==0 && h.last==second_end);
    tap_text(ctx,"First");assert(h.selected && h.first==second && h.last==second_end);
    tap_text(ctx,"Second");assert(!h.selected);
    tap_text(ctx,"Second");tap_text(ctx,"Third");assert(h.first==second && h.last==strlen(text));
    tap_text(ctx,"Third");assert(h.first==second && h.last==second_end);
    tap_text(ctx,"First");assert(h.first==0 && h.last==second_end);
    assert(on_key(ctx,UI_KEY_2)==APP_REDRAW_PAGE && !h.active && !h.mark_count);
    open_selection(ctx);tap_text(ctx,"Second");save_selection(ctx);assert(!h.active);assert_stored(second,second_end);
}
static void test_remove_selected_sentences(app_ctx_t *ctx) {
    const char *text="First. Second. Third.";
    fixture(ctx,"remove-selected",text,40);
    open_selection(ctx);tap_text(ctx,"First");tap_text(ctx,"Second");tap_text(ctx,"Third");save_selection(ctx);
    assert_stored(0,strlen(text));
    open_selection(ctx);tap_text(ctx,"Second");preview(ctx,"remove-sentence-selected");
    save_selection(ctx);assert(!h.active);
    reader_highlight_range_t out[8];size_t n=0;
    assert(reader_highlight_load(s_path,s_file_size,0,s_text,s_text_len,out,8,&n));
    // 取消中间句后两边仍保留，不得把整条摘录删掉。/ Removing the middle sentence preserves both surrounding fragments.
    size_t begin=strlen("First. "),end=strlen("First. Second.");
    assert(n==2);
    for(size_t i=0;i<n;i++)assert(out[i].end<=begin || out[i].start>=end);
    assert(h.mark_count==2);preview(ctx,"remove-sentence-complete");
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len);
    assert(h.mark_count==2);
    open_selection(ctx);tap_text(ctx,"First");size_t before=h.mark_count;
    sim_sd_set_present(false);save_selection(ctx);assert(h.active && h.message && h.mark_count==before);
    sim_sd_set_present(true);assert(read_pico_sd_remount()==ESP_OK);save_selection(ctx);assert(!h.active);
    open_selection(ctx);tap_text(ctx,"Third");save_selection(ctx);assert(!h.active);
    // 可能剩下纯空白记录，但不能再有可见字的划线。/ Any remaining whitespace must not underline a visible glyph.
    for(size_t i=0;i<h.mark_count;i++)assert(reader_sentence_whitespace(s_text+h.marks[i].start,h.marks[i].end-h.marks[i].start));
    open_selection(ctx);tap_text(ctx,"Second");save_selection(ctx);assert(!h.active);
    assert(h.mark_count>=1);
}

static void test_remove_cached_coverage(app_ctx_t *ctx) {
    const char *text="First.\n\nSecond. Third.";
    fixture(ctx,"remove-coverage",text,40);
    open_selection(ctx);tap_text(ctx,"First");save_selection(ctx);
    open_selection(ctx);tap_text(ctx,"Second");save_selection(ctx);
    open_selection(ctx);tap_text(ctx,"First");tap_text(ctx,"Second");
    assert(selection_is_marked());
    tap_text(ctx,"Third");assert(!selection_is_marked());
    // 部分已划线加未划线：补划整段，再反向取消，重叠旧记录不得复活。
    // Mixed marked/unmarked selection adds the whole range; later removal must subtract overlapping old records too.
    save_selection(ctx);open_selection(ctx);tap_text(ctx,"First");tap_text(ctx,"Second");tap_text(ctx,"Third");
    assert(selection_is_marked());save_selection(ctx);assert(!h.active && !h.mark_count);
    reader_highlight_range_t out[8];size_t n=99;
    assert(reader_highlight_load(s_path,s_file_size,0,s_text,s_text_len,out,8,&n) && !n);
    // 缓存加载失败后恢复，不能把屏幕原“划线”按钮的一次确认偷偷当删除。
    // Recovering an unknown cache must not silently reinterpret one add confirmation as deletion.
    open_selection(ctx);tap_text(ctx,"First");save_selection(ctx);
    sim_sd_set_present(false);
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len);
    assert(!h.loaded);open_selection(ctx);tap_text(ctx,"First");
    sim_sd_set_present(true);assert(read_pico_sd_remount()==ESP_OK);
    save_selection(ctx);assert(h.active && h.loaded && selection_is_marked());assert_stored(0,strlen("First."));
    save_selection(ctx);assert(!h.active && !h.mark_count);
}

static void test_single_pass(app_ctx_t *ctx) {
    char text[2048]="";for(int i=0;i<80;i++)strcat(text,"句子。");fixture(ctx,"many-marks",text,36);
    // 分离区间不合并，用真实存储构造大量同页划线。/ Keep disjoint ranges to exercise many actual stored marks on one page.
    for(size_t i=0;i<strlen(text);i+=strlen("句子。"))
        assert(reader_highlight_save(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len,i,i+strlen("句")));
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len);
    assert(h.loaded && h.mark_count==80);assert_marks_on_page(ctx,0);
}
static void test_failures_and_lifecycle(app_ctx_t *ctx) {
    fixture(ctx,"failures","First sentence. Second sentence.",48);open_selection(ctx);tap_text(ctx,"First");
    sim_sd_set_present(false);save_selection(ctx);assert(h.active && h.message && !h.mark_count);
    reader_on_exit(ctx);assert(!h.active);sim_sd_set_present(true);assert(read_pico_sd_remount()==ESP_OK);
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len);assert(h.loaded && !h.mark_count);
    open_selection(ctx);tap_text(ctx,"Second");reader_on_exit(ctx);assert(!h.active);
    sim_sd_set_present(false);reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,s_text,s_text_len);assert(!h.loaded);
    open_selection(ctx);tap_text(ctx,"Second");size_t saves=save_calls;save_selection(ctx);assert(h.active && save_calls==saves);
    sim_sd_set_present(true);assert(read_pico_sd_remount()==ESP_OK);save_selection(ctx);assert(!h.active && h.loaded);
    assert_stored(strlen("First sentence. "),strlen(s_text));
    reader_highlight_ui_chapter(s_path,s_book_title,s_file_size,0,s_chapter_title,"Different chapter text",22);assert(h.loaded && !h.mark_count);
}
int main(void) {
    e0470_waveform_init();
    epd_init(&epd_board_read_pico,&E0470_DISPLAY,EPD_LUT_1K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    app_settings_init();assert(ttf_font_init()==ESP_OK);assert(ttf_font_open("/flash/fonts/Fallback.ttf")==ESP_OK);
    EpdiyHighlevelState hl=epd_hl_init(&READER_WAVEFORM);
    app_ctx_t ctx={.hl=&hl,.fb=epd_hl_get_framebuffer(&hl),.now_ms=10000};assert(read_pico_sd_remount()==ESP_OK);
    test_sentence_boxes(&ctx);test_remove_selected_sentences(&ctx);test_remove_cached_coverage(&ctx);test_paragraph_neighbors(&ctx);test_whole_sentence(&ctx);test_unicode_neighbors(&ctx);test_single_pass(&ctx);test_failures_and_lifecycle(&ctx);
    free_book();
    puts("highlight UI: entry/selected sentence boxes, cross-paragraph adjacency, whole chapter sentence across pages, neighbors, Unicode deselection, reflow/reload, single geometry pass, SD failure/retry and lifecycle PASS");
    return 0;
}
