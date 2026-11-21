/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实选词模块的缓存几何、避让和换行回归，不启动模拟器。/ English: Real dictionary UI cached-geometry, clearance and row-selection tests without simulator startup.
 */
#include <assert.h>
#include <stdio.h>
#include "../main/dictionary/reader_dictionary_ui.c"
static int64_t test_ms;
static unsigned layout_calls,font_calls,queries,records;
static const char fixture[]="alpha beta\ngamma delta\nepsilon zeta";
static book_layout_glyph_t glyphs[64];
static size_t glyph_count;
static uint8_t framebuffer[684*1216/2];
int64_t esp_timer_get_time(void){return test_ms*1000;}
int epd_width(void){return 684;}
int epd_height(void){return 1216;}
EpdRect ui_rect_union(EpdRect a,EpdRect b){int l=a.x<b.x?a.x:b.x,t=a.y<b.y?a.y:b.y,r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,bt=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){l,t,r-l,bt-t};}
bool ui_rect_hit(EpdRect a,uint16_t x,uint16_t y){return x>=a.x&&y>=a.y&&x<a.x+a.width&&y<a.y+a.height;}
int ttf_text_width_px(int px,const char *text){++font_calls;return(int)strlen(text)*px/2;}
bool ttf_font_has_glyph(uint32_t cp){(void)cp;++font_calls;return true;}
void ui_text(uint8_t*f,int x,int y,int px,const char*t,enum EpdFontFlags a,bool inv){(void)f;(void)x;(void)y;(void)px;(void)t;(void)a;(void)inv;}
size_t book_layout_page_start_offset(size_t page){return page?strlen(fixture):0;}
bool book_layout_visit_page(size_t page,EpdRect rect,int px,book_layout_glyph_visitor_t visitor,void*ctx){(void)rect;(void)px;assert(!page);++layout_calls;for(size_t i=0;i<glyph_count;++i)if(!visitor(&glyphs[i],ctx))return false;return true;}
reader_dict_status_t reader_dictionary_session_open(reader_dict_session_t*s,const char*path){(void)path;s->status=READER_DICT_OK;return READER_DICT_OK;}
reader_dict_status_t reader_dictionary_session_lookup(reader_dict_session_t*s,const char*key,reader_dict_entry_t*entry){(void)s;++queries;memset(entry,0,sizeof(*entry));snprintf(entry->word,sizeof(entry->word),"%s",key);return READER_DICT_OK;}
reader_dict_status_t reader_dictionary_session_close(reader_dict_session_t*s){(void)s;return READER_DICT_OK;}
reader_dict_status_t reader_dictionary_lookup(const char*key,reader_dict_entry_t*entry){return reader_dictionary_session_lookup(NULL,key,entry);}
bool reader_vocabulary_record(const char*q,const char*b,reader_dict_status_t status){(void)q;(void)b;(void)status;++records;return true;}
static void geometry(void){
    glyph_count=0;int x=60,y=100;
    for(size_t i=0;i<strlen(fixture);++i){
        if(fixture[i]=='\n'){x=60;y+=60;continue;}
        if(fixture[i]==' '){x=140;continue;}
        glyphs[glyph_count++]=(book_layout_glyph_t){.start=i,.end=i+1,.rect={x,y,8,36},.px=36};x+=8;
    }
}
static void begin(void){
    reader_dict_ui_close();geometry();test_ms=1000;layout_calls=font_calls=queries=records=0;
    assert(reader_dict_ui_hold_at(fixture,strlen(fixture),0,(EpdRect){40,80,604,300},36,framebuffer,1100,76,118));
    assert(d.hold_line==100&&d.hold_first==0&&!queries&&!records);
}
static bool move(int x,int y,int64_t ms){test_ms=ms;return reader_dict_ui_hold_move(x,y);}
static void tick(int64_t ms){test_ms=ms;reader_dict_ui_hold_tick(ms);}
static bool contains(EpdRect a,EpdRect b){return a.x<=b.x&&a.y<=b.y&&a.x+a.width>=b.x+b.width&&a.y+a.height>=b.y+b.height;}
static void settle(int y){assert(!move(76,y,1010));tick(1130);}
int main(void){
    begin();unsigned layouts=layout_calls,fonts=font_calls;
    // 第一段向下避让即使跨多行仍锁住，只有稳定120ms以后才采用相对纵移。
    // The complete first downward escape stays locked across multiple rows; only later relative movement after 120ms settling changes rows.
    assert(!move(76,300,1010));assert(!move(76,500,1050));tick(1169);assert(d.hold_line==100);
    tick(1170);assert(move(76,550,1180));assert(d.hold_line==160&&d.hold_first==(size_t)(strstr(fixture,"gamma")-fixture));
    assert(d.hold_cue.y==d.hold_cue_right.y&&d.hold_cue.y>100);
    assert(layout_calls==layouts&&font_calls==fonts&&!queries&&!records);
    assert(move(156,550,1190)&&d.hold_first==(size_t)(strstr(fixture,"delta")-fixture));
    assert(reader_dict_ui_hold_finish()&&!strcmp(d.query,"delta")&&queries==1&&records==1);
    assert(!reader_dict_ui_hold_finish()&&queries==1&&records==1);
    // 换行迟滞：边界附近几像素抖动不能来回跳；反向超过阈值才能回上一行。
    // Row hysteresis prevents oscillation near a midpoint and permits a deliberate reverse crossing.
    begin();settle(500);assert(move(76,550,1140)&&d.hold_line==160);
    assert(!move(76,532,1150)&&d.hold_line==160);assert(!move(76,530,1160)&&d.hold_line==160);
    assert(move(76,520,1170)&&d.hold_line==100);
    // 首次向上也只是避让；长时间按住但未纵移，不提前解除第一段保护。
    // An upward first escape also clears the finger; holding still before any vertical movement never arms early.
    begin();tick(2000);assert(!move(76,10,2010)&&d.hold_line==100);tick(2130);
    assert(move(76,70,2140)&&d.hold_line==160);
    // 抖动带内稳定有效，持续大幅移动或反向则继续保护原行。
    // Small jitter may settle, but continued larger movement or reversal keeps the initial row protected.
    begin();assert(!move(76,500,1010));assert(!move(76,502,1080));tick(1130);
    assert(move(76,560,1140)&&d.hold_line==160);
    begin();assert(!move(76,500,1010));assert(!move(76,510,1100));tick(1219);
    assert(!move(76,300,1220)&&d.hold_line==100);tick(1340);assert(move(76,360,1350)&&d.hold_line==160);
    // 指尖可以离开正文；只遍历已缓存本页行，远端位置钳到首末可见目标。
    // The finger may leave the body; only cached page rows participate and extreme positions clamp to visible targets.
    begin();settle(800);assert(move(76,860,1140)&&d.hold_line==160);assert(move(76,1200,1150)&&d.hold_line==220);
    assert(move(0,0,1160)&&d.hold_line==100);assert(!queries&&!records);
    EpdRect trail=reader_dict_ui_hold_restore_area();assert(trail.y<=98&&trail.y+trail.height>=258);
    assert(reader_dict_ui_hold_cancel()&&!queries&&!records&&!reader_dict_ui_holding());
    // 先前词卡的隐藏行不可被选；取消保留同一词卡与整段轨迹。
    // Rows behind an existing card remain unselectable; cancellation retains the original card and complete trail.
    begin();assert(reader_dict_ui_hold_finish());assert(d.card);unsigned before=queries;
    assert(reader_dict_ui_hold_existing(76,118));d.hold_visible.height=120;
    EpdRect old_cue=d.hold_cue;layouts=layout_calls;fonts=font_calls;
    settle(500);assert(move(76,1000,1140)&&d.hold_line==160);
    assert(contains(reader_dict_ui_hold_area(),old_cue)&&contains(reader_dict_ui_hold_area(),d.hold_cue));
    assert(layout_calls==layouts&&font_calls==fonts&&queries==before);
    assert(reader_dict_ui_hold_cancel()&&d.card&&queries==before);
    // 同一个英文词跨行时可移动框到另一片段，但松手仍查询完整原词一次。
    // A wrapped English word can frame its other row segment while release still queries the complete source word once.
    reader_dict_ui_close();geometry();test_ms=1000;queries=records=0;
    for(size_t i=0;i<glyph_count;++i) {
        if(glyphs[i].start<5) {
            size_t at=glyphs[i].start;
            glyphs[i].rect=(EpdRect){60+(int)(at<2?at:at-2)*8,at<2?100:160,8,36};
        } else glyphs[i].rect.y+=120;
    }
    assert(reader_dict_ui_hold_at(fixture,strlen(fixture),0,(EpdRect){40,80,604,400},36,framebuffer,1100,64,118));
    assert(d.hold_first==0&&d.hold_end==5&&d.hold_line==100);
    assert(!move(64,500,1010));tick(1130);assert(move(64,560,1140));
    assert(d.hold_first==0&&d.hold_end==5&&d.hold_line==160&&!queries&&!records);
    assert(reader_dict_ui_hold_finish()&&!strcmp(d.query,"alpha")&&queries==1&&records==1);
    assert(reader_dict_ui_hold_existing(64,178)&&d.hold_line==160);
    assert(!move(64,50,1150));tick(1270);assert(move(64,0,1280)&&d.hold_line==100&&d.hold_end==5);
    assert(reader_dict_ui_hold_cancel()&&d.card&&queries==1);
    reader_dict_ui_close();puts("dictionary rows: first escape, settling, reverse movement, hysteresis, cached page/card bounds, cues and one release passed");
}
