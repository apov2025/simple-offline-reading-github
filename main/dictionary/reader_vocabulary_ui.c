/* SPDX-License-Identifier: Apache-2.0
 * 紧凑单词本：最近查询在前，当前词库释义直接展示；仅翻页时读取，不写记录。
 * Compact notebook: most recent first with current definitions inline; load on page changes, never write records.
 */
#include "reader_vocabulary_ui.h"
#include "reader_vocabulary.h"
#include "board/reader_usb.h"
#include "read_pico_sd.h"
#include "ui_kit.h"
#include "ttf_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NB_ROWS=5, NB_TOP=130, NB_ROW_HEIGHT=190, NB_FOOTER=1120, NB_PAD=30, NB_LINE_BYTES=256, NB_MEANING_BYTES=1600, NB_DETAIL_TARGET=56 };
static struct {
    bool active,readable,detail;
    size_t page,count,total,detail_row;
    reader_vocabulary_record_t rows[NB_ROWS];
    char meanings[NB_ROWS][2][NB_LINE_BYTES];
    char detail_meanings[NB_ROWS][NB_MEANING_BYTES];
} notebook;

static size_t vocabulary_utf8_step(const char *text) {
    size_t n=1;
    while(text[n] && ((unsigned char)text[n]&0xc0)==0x80)++n;
    return n;
}
static void vocabulary_ellipsis(char *line,size_t cap,int px,int width) {
    size_t n=strlen(line);
    while(n && (n+4>cap || ttf_text_width_px(px,line)+ttf_text_width_px(px,"…")>width)) {
        do { --n; }while(n && ((unsigned char)line[n]&0xc0)==0x80);
        line[n]=0;
    }
    if(n+4<=cap)strcat(line,"…");
}
static size_t vocabulary_line(const char *text,char *out,size_t cap,int px,int width) {
    size_t n=0;
    while(text[n] && text[n]!='\n') {
        size_t k=vocabulary_utf8_step(text+n);
        if(n+k>=cap)break;
        memcpy(out+n,text+n,k);out[n+k]=0;
        if(n && ttf_text_width_px(px,out)>width)break;
        n+=k;
    }
    out[n]=0;return n+(text[n]=='\n');
}
static bool vocabulary_media_ready(void) {
    if(reader_usb_active())return false;
    read_pico_sd_info_t info={0};
    return read_pico_sd_get_info(&info)==ESP_OK && info.present && info.mounted;
}
static void vocabulary_meaning(size_t row,const char *text) {
    memset(notebook.meanings[row],0,sizeof(notebook.meanings[row]));
    snprintf(notebook.detail_meanings[row],NB_MEANING_BYTES,"%s",text);
    for(size_t line=0;line<2 && *text;line++) {
        size_t used=vocabulary_line(text,notebook.meanings[row][line],NB_LINE_BYTES,32,UI_LOCK_WIDTH-2*NB_PAD);
        if(!used)break;
        text+=used;
        if(line==1 && *text)vocabulary_ellipsis(notebook.meanings[row][line],NB_LINE_BYTES,32,UI_LOCK_WIDTH-2*NB_PAD);
    }
}
static void vocabulary_load_page(void) {
    memset(notebook.meanings,0,sizeof(notebook.meanings));
    memset(notebook.detail_meanings,0,sizeof(notebook.detail_meanings));
    notebook.readable=reader_vocabulary_page(notebook.page*NB_ROWS,notebook.rows,NB_ROWS,&notebook.count,&notebook.total);
    if(!notebook.readable) { notebook.count=notebook.total=0;return; }
    if(!notebook.count)return;
    // 同页只开一次词库，结束前关闭；详情和绘制仅使用有界缓存。/ One session per page, closed before return; details and drawing use bounded caches only.
    reader_dict_entry_t *entry=malloc(sizeof(*entry));
    reader_dict_session_t session={0};
    bool opened=entry && vocabulary_media_ready();
    reader_dict_status_t opened_status=opened?reader_dictionary_session_open(&session,NULL):READER_DICT_IO_ERROR;
    for(size_t i=0;i<notebook.count;i++) {
        if(!entry) { vocabulary_meaning(i,"暂时无法读取释义");continue; }
        if(!vocabulary_media_ready()) { vocabulary_meaning(i,"无法读取词典");continue; }
        reader_dict_status_t status=opened_status==READER_DICT_OK?
            reader_dictionary_session_lookup(&session,notebook.rows[i].query,entry):opened_status;
        if(status==READER_DICT_OK && entry->count) {
            char text[NB_MEANING_BYTES];
            if(entry->count>1)snprintf(text,sizeof(text),"1. %s  2. %s",entry->senses[0].gloss,entry->senses[1].gloss);
            else snprintf(text,sizeof(text),"%s",entry->senses[0].gloss);
            vocabulary_meaning(i,text);
        } else {
            const char *message=status==READER_DICT_NOT_FOUND?"未收录":status==READER_DICT_MISSING?"未找到词典":
                status==READER_DICT_INVALID?"词典文件损坏":"无法读取词典";
            vocabulary_meaning(i,message);
        }
    }
    if(opened && reader_dictionary_session_close(&session)!=READER_DICT_OK)
        for(size_t i=0;i<notebook.count;i++)vocabulary_meaning(i,"无法读取词典");
    free(entry);
}
void reader_vocabulary_ui_open(void) { memset(&notebook,0,sizeof(notebook));notebook.active=true;vocabulary_load_page(); }
void reader_vocabulary_ui_close(void) { memset(&notebook,0,sizeof(notebook)); }
bool reader_vocabulary_ui_active(void) { return notebook.active; }
static void vocabulary_clipped(uint8_t *fb,int x,int y,int px,const char *text,int width) {
    char line[NB_LINE_BYTES];size_t used=vocabulary_line(text,line,sizeof(line),px,width);
    if(text[used])vocabulary_ellipsis(line,sizeof(line),px,width);
    ui_text(fb,x,y,px,line,EPD_DRAW_ALIGN_LEFT,false);
}
static EpdRect vocabulary_detail_target(size_t row) {
    return (EpdRect){UI_LOCK_WIDTH-NB_PAD-NB_DETAIL_TARGET,NB_TOP+(int)row*NB_ROW_HEIGHT+30,NB_DETAIL_TARGET,NB_DETAIL_TARGET};
}
static void vocabulary_paragraph(uint8_t *fb,int y,int px,int spacing,int lines,const char *text) {
    for(int i=0;i<lines && *text;i++) {
        char line[NB_LINE_BYTES];size_t used=vocabulary_line(text,line,sizeof(line),px,UI_LOCK_WIDTH-2*NB_PAD);
        if(!used)break;
        text+=used;
        if(i==lines-1 && *text)vocabulary_ellipsis(line,sizeof(line),px,UI_LOCK_WIDTH-2*NB_PAD);
        ui_text(fb,NB_PAD,y+i*spacing,px,line,EPD_DRAW_ALIGN_LEFT,false);
    }
}
bool reader_vocabulary_ui_back(void) {
    if(!notebook.active || !notebook.detail)return false;
    notebook.detail=false;return true;
}
static void vocabulary_detail_draw(uint8_t *fb) {
    size_t row=notebook.detail_row;const reader_vocabulary_record_t *r=&notebook.rows[row];
    vocabulary_paragraph(fb,178,44,56,2,r->query);
    vocabulary_paragraph(fb,320,36,48,8,notebook.detail_meanings[row]);
    ui_text(fb,NB_PAD,802,26,"最近阅读",EPD_DRAW_ALIGN_LEFT,false);
    vocabulary_paragraph(fb,852,32,42,4,*r->book?r->book:"未标书名");
    char count[48];snprintf(count,sizeof(count),"查询 %lu 次",(unsigned long)r->count);
    ui_text(fb,NB_PAD,1050,32,count,EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,UI_LOCK_WIDTH/2,NB_FOOTER+45,28,"返回列表",EPD_DRAW_ALIGN_CENTER,false);
}
void reader_vocabulary_ui_draw(uint8_t *fb) {
    ui_clear_rect_fast(fb,(EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT});
    ui_text(fb,NB_PAD,58,34,"单词本",EPD_DRAW_ALIGN_LEFT,false);
    if(notebook.detail) {
        ui_hairline(fb,106,NB_PAD,UI_LOCK_WIDTH-2*NB_PAD,0xc0);
        vocabulary_detail_draw(fb);return;
    }
    char count[48];snprintf(count,sizeof(count),"%zu 项",notebook.total);
    ui_text(fb,UI_LOCK_WIDTH-NB_PAD,64,24,count,EPD_DRAW_ALIGN_RIGHT,false);
    ui_hairline(fb,106,NB_PAD,UI_LOCK_WIDTH-2*NB_PAD,0xc0);
    if(!notebook.readable)ui_text(fb,NB_PAD,182,28,"无法读取单词本，请检查存储卡",EPD_DRAW_ALIGN_LEFT,false);
    else if(!notebook.count)ui_text(fb,NB_PAD,182,28,"还没有查词记录",EPD_DRAW_ALIGN_LEFT,false);
    for(size_t i=0;i<notebook.count;i++) {
        const reader_vocabulary_record_t *r=&notebook.rows[i];int y=NB_TOP+(int)i*NB_ROW_HEIGHT;
        vocabulary_clipped(fb,NB_PAD,y+40,36,r->query,UI_LOCK_WIDTH-2*NB_PAD-NB_DETAIL_TARGET-16);
        EpdRect target=vocabulary_detail_target(i);int cx=target.x+target.width/2,cy=target.y+target.height/2;
        for(int stroke=0;stroke<2;stroke++) {
            epd_draw_line(cx-5+stroke,cy-9,cx+4+stroke,cy,UI_GRAY_BLACK,fb);
            epd_draw_line(cx+4+stroke,cy,cx-5+stroke,cy+9,UI_GRAY_BLACK,fb);
        }
        for(size_t line=0;line<2;line++)if(*notebook.meanings[i][line])
            ui_text(fb,NB_PAD,y+100+(int)line*42,32,notebook.meanings[i][line],EPD_DRAW_ALIGN_LEFT,false);
        ui_hairline(fb,y+NB_ROW_HEIGHT-8,NB_PAD,UI_LOCK_WIDTH-2*NB_PAD,0xe0);
    }
    if(notebook.page)ui_text(fb,NB_PAD,NB_FOOTER+45,26,"上一页",EPD_DRAW_ALIGN_LEFT,false);
    if((notebook.page+1)*NB_ROWS<notebook.total)ui_text(fb,UI_LOCK_WIDTH-NB_PAD,NB_FOOTER+45,26,"下一页",EPD_DRAW_ALIGN_RIGHT,false);
    snprintf(count,sizeof(count),"%zu / %zu",notebook.page+1,notebook.total?(notebook.total+NB_ROWS-1)/NB_ROWS:1);
    ui_text(fb,UI_LOCK_WIDTH/2,NB_FOOTER+45,24,count,EPD_DRAW_ALIGN_CENTER,false);
}
bool reader_vocabulary_ui_move(int direction) {
    if(!notebook.active || notebook.detail)return false;
    if(direction<0 && notebook.page)--notebook.page;
    else if(direction>0 && (notebook.page+1)*NB_ROWS<notebook.total)++notebook.page;
    else return false;
    vocabulary_load_page();return true;
}
bool reader_vocabulary_ui_tap(uint16_t x,uint16_t y,uint8_t *fb) {
    (void)fb;
    if(!notebook.active)return false;
    if(notebook.detail)return y>=NB_FOOTER && reader_vocabulary_ui_back();
    for(size_t i=0;i<notebook.count;i++) {
        EpdRect target=vocabulary_detail_target(i);
        if(x>=target.x && x<target.x+target.width && y>=target.y && y<target.y+target.height) {
            notebook.detail=true;notebook.detail_row=i;return true;
        }
    }
    if(y>=NB_FOOTER) {
        if(x<UI_LOCK_WIDTH/3)return reader_vocabulary_ui_move(-1);
        if(x>UI_LOCK_WIDTH*2/3)return reader_vocabulary_ui_move(1);
    }
    return false;
}
