/* SPDX-License-Identifier: Apache-2.0
 * 原页整句框选、局部取消与保存后的细实线，不重排、不查询词库。/ Thin sentence boxes and persistent solid underlines on the original page; no reflow or dictionary queries.
 * 冻结：点选不翻页；句子可延伸到章节内前后页，绘制不读写SD，只有“划线”或“取消划线”确认才写入，选句不显示行提示点。
 * Frozen: selection never turns pages; sentences may extend across pages within the chapter; drawing does no SD I/O and only explicit add/remove confirmation persists; sentence selection has no row cues.
 */
#include "reader_highlight_ui.h"
#include "reader_highlight_store.h"
#include "reader_sentence.h"
#include "board/reader_clock.h"
#include "book_layout.h"
#include "ui_kit.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

enum { MARK_CAP=4096,CELL_CAP=4096 };
typedef struct { size_t first,end;EpdRect rect;size_t sentence_first,sentence_end; } mark_cell_t;
static struct {
 char path[1025],title[513],chapter_title[513];uint32_t bytes;size_t chapter,len;
 const char *text;reader_highlight_range_t *marks;size_t mark_count;bool loaded;
 mark_cell_t *cells;size_t n,capacity,start,end,focus,first,last;
 bool selected,active;
 uint8_t *snapshot,*fb;size_t frame_bytes;EpdRect body;int footer;
 const char *message;
} h;
static void *allocate(size_t bytes) {
#if defined(ESP_PLATFORM)
 return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 return malloc(bytes);
#endif
}
void reader_highlight_ui_close(void) {
 if(h.snapshot && h.fb)memcpy(h.fb,h.snapshot,h.frame_bytes);
 free(h.cells);free(h.snapshot);h.cells=NULL;h.snapshot=NULL;h.fb=NULL;h.n=h.capacity=0;
 h.active=h.selected=false;h.message=NULL;
}
void reader_highlight_ui_reset(void) { reader_highlight_ui_close();free(h.marks);memset(&h,0,sizeof(h)); }
static int range_order(const void *a,const void *b) {
 const reader_highlight_range_t *x=a,*y=b;
 return x->start<y->start?-1:x->start>y->start?1:x->end<y->end?-1:x->end>y->end;
}
static void compact_marks(void) {
 if(!h.mark_count)return;
 qsort(h.marks,h.mark_count,sizeof(*h.marks),range_order);
 size_t n=0;for(size_t i=0;i<h.mark_count;i++) {
  if(n && h.marks[i].start<=h.marks[n-1].end){if(h.marks[i].end>h.marks[n-1].end)h.marks[n-1].end=h.marks[i].end;}
  else h.marks[n++]=h.marks[i];
 }h.mark_count=n;
}
void reader_highlight_ui_chapter(const char *path,const char *title,uint32_t bytes,size_t chapter,const char *chapter_title,const char *text,size_t len) {
 reader_highlight_ui_reset();
 if(!path || !title || !chapter_title || !text || strlen(path)>=sizeof(h.path) || strlen(title)>=sizeof(h.title) || strlen(chapter_title)>=sizeof(h.chapter_title))return;
 strcpy(h.path,path);strcpy(h.title,title);strcpy(h.chapter_title,chapter_title);
 h.bytes=bytes;h.chapter=chapter;h.text=text;h.len=len;
 h.marks=allocate(MARK_CAP*sizeof(*h.marks));
 if(h.marks)h.loaded=reader_highlight_load(path,bytes,chapter,text,len,h.marks,MARK_CAP,&h.mark_count);
 compact_marks();
}
bool reader_highlight_ui_active(void) { return h.active; }
static bool count_cell(const book_layout_glyph_t *g,void *ctx) { (void)g;size_t *n=ctx;return ++*n<=CELL_CAP; }
static bool add_cell(const book_layout_glyph_t *g,void *ctx) {
 (void)ctx;if(h.n>=h.capacity || g->start<h.start || g->end>h.end || g->start>=g->end)return false;
 // 某些 Unicode 空白有字形占位，但不能成为句框或独立点击目标。
 // Some Unicode whitespace has a layout glyph; exclude it from sentence boxes and tap targets.
 if(reader_sentence_whitespace(h.text+g->start,g->end-g->start))return true;
 size_t first,last;
 if(h.n && g->start>=h.cells[h.n-1].sentence_first && g->start<h.cells[h.n-1].sentence_end) {
  first=h.cells[h.n-1].sentence_first;last=h.cells[h.n-1].sentence_end;
 } else if(!reader_sentence_span(h.text,h.len,0,h.len,g->start,&first,&last))return false;
 h.cells[h.n++]=(mark_cell_t){g->start,g->end,g->rect,first,last};return true;
}
bool reader_highlight_ui_open(size_t page,EpdRect body,int px,uint8_t *fb,int footer) {
 reader_highlight_ui_close();if(!h.text || !fb)return false;
 size_t count=0;h.start=book_layout_page_start_offset(page);h.end=book_layout_page_start_offset(page+1);
 if(h.start>=h.end || h.end>h.len || !book_layout_visit_page(page,body,px,count_cell,&count) || !count)return false;
 h.frame_bytes=(size_t)epd_width()*epd_height()/2;
 h.snapshot=allocate(h.frame_bytes);h.cells=allocate(count*sizeof(*h.cells));h.capacity=count;
 if(!h.snapshot || !h.cells || !book_layout_visit_page(page,body,px,add_cell,NULL) || !h.n){reader_highlight_ui_close();return false;}
 h.body=body;h.footer=footer;h.fb=fb;memcpy(h.snapshot,fb,h.frame_bytes);h.active=true;return true;
}
static size_t nearest(uint16_t x,uint16_t y) {
 size_t best=0;int best_row=INT_MAX,best_x=INT_MAX;
 for(size_t i=0;i<h.n;i++) {
  EpdRect r=h.cells[i].rect;int dy=abs(2*(int)y-(2*r.y+r.height));
  int dx=x<r.x?r.x-x:x>=r.x+r.width?x-r.x-r.width+1:0;
  if(dy<best_row || (dy==best_row && dx<best_x)){best=i;best_row=dy;best_x=dx;}
 }
 return best;
}
static bool whitespace_between(size_t a,size_t b) { return reader_sentence_whitespace(h.text+a,b-a); }
// 整句反选后沿 UTF-8 边界去空白，与句界模块使用相同字符集合。
// Trim deselected sentence boundaries by UTF-8 codepoint using the sentence parser's whitespace set.
static void trim_selection(void) {
 while(h.first<h.last) {
  size_t next=h.first+1;while(next<h.last && ((unsigned char)h.text[next]&0xc0)==0x80)next++;
  if(!whitespace_between(h.first,next))break;
  h.first=next;
 }
 while(h.last>h.first) {
  size_t prior=h.last-1;while(prior>h.first && ((unsigned char)h.text[prior]&0xc0)==0x80)prior--;
  if(!whitespace_between(prior,h.last))break;
  h.last=prior;
 }
}
// 忽略句间空白判断已划线覆盖，只读取当前章节缓存。/ Check cached coverage while ignoring whitespace between sentences.
static bool selection_is_marked(void) {
 if(!h.selected || !h.loaded)return false;
 size_t cursor=h.first;
 for(size_t i=0;i<h.mark_count && cursor<h.last;i++) {
  reader_highlight_range_t r=h.marks[i];
  if(r.end<=cursor)continue;
  if(r.start>=h.last)break;
  if(r.start>cursor && !whitespace_between(cursor,r.start))return false;
  if(r.end>cursor)cursor=r.end;
 }
 return cursor>=h.last || whitespace_between(cursor,h.last);
}
static bool remove_selection(void) {
 // 写卡之前分配下一份缓存，避免已删除但内存不足时仍显示旧线。/ Allocate the next cache before committing to avoid stale visible marks after an allocation failure.
 reader_highlight_range_t *next=allocate(MARK_CAP*sizeof(*next));if(!next)return false;
 size_t count=0;
 for(size_t i=0;i<h.mark_count;i++) {
  reader_highlight_range_t r=h.marks[i],parts[2];size_t n=0;
  if(r.end<=h.first || r.start>=h.last)parts[n++]=r;
  else {
   if(r.start<h.first)parts[n++]=(reader_highlight_range_t){r.start,(uint32_t)h.first};
   if(r.end>h.last)parts[n++]=(reader_highlight_range_t){(uint32_t)h.last,r.end};
  }
  if(count+n>MARK_CAP){free(next);return false;}
  for(size_t j=0;j<n;j++)next[count++]=parts[j];
 }
 if(!reader_highlight_remove(h.path,h.bytes,h.chapter,h.text,h.len,h.first,h.last)){free(next);return false;}
 free(h.marks);h.marks=next;h.mark_count=count;return true;
}
bool reader_highlight_ui_tap(uint16_t x,uint16_t y) {
 if(!h.active)return false;
 if(y>=h.footer) {
  if(x<UI_LOCK_WIDTH/3){reader_highlight_ui_close();return true;}
  if(x<UI_LOCK_WIDTH*2/3 || !h.selected)return false;
  if(!h.loaded && h.marks) {
   h.loaded=reader_highlight_load(h.path,h.bytes,h.chapter,h.text,h.len,h.marks,MARK_CAP,&h.mark_count);
   if(h.loaded) {
    compact_marks();
    // 刚恢复读取时先展示取消按钮，不把原来的添加确认变成删除。/ After recovering the cache, show removal before interpreting an add confirmation as a delete.
    if(selection_is_marked()){h.message=NULL;return true;}
   }
  }
  if(h.loaded)compact_marks();
  if(selection_is_marked()) {
   if(!remove_selection()){h.message="未写入，请检查SD卡";return true;}
   reader_highlight_ui_close();return true;
  }
  int64_t epoch=0;(void)reader_clock_trusted_epoch(&epoch);
  if(!h.loaded || !reader_highlight_save_at(h.path,h.title,h.bytes,h.chapter,h.chapter_title,h.text,h.len,h.first,h.last,epoch)) { h.message="未写入，请检查SD卡";return true; }
  bool found=false;for(size_t i=0;i<h.mark_count;i++)if(h.marks[i].start==h.first && h.marks[i].end==h.last)found=true;
  if(!found && h.mark_count<MARK_CAP)h.marks[h.mark_count++]=(reader_highlight_range_t){(uint32_t)h.first,(uint32_t)h.last};
  compact_marks();reader_highlight_ui_close();return true;
 }
 if(!ui_rect_hit(h.body,x,y))return false;
 size_t hit=nearest(x,y),first=h.cells[hit].sentence_first,last=h.cells[hit].sentence_end;
 EpdRect r=h.cells[hit].rect;
 int dx=x<r.x?r.x-x:x>=r.x+r.width?x-r.x-r.width+1:0;
 int dy=y<r.y?r.y-y:y>=r.y+r.height?y-r.y-r.height+1:0;
 if(dx*dx+dy*dy>32*32)return false;
 h.message=NULL;h.focus=hit;
 if(!h.selected){h.first=first;h.last=last;h.selected=true;}
 else if(first==h.first && last==h.last)h.selected=false;
 else if(first>=h.last && whitespace_between(h.last,first))h.last=last;
 else if(last<=h.first && whitespace_between(last,h.first))h.first=first;
 else if(first==h.first && last<h.last) { h.first=last;trim_selection(); }
 else if(last==h.last && first>h.first) { h.last=first;trim_selection(); }
 else if(first<h.first || last>h.last){h.first=first;h.last=last;}
 return true;
}
static EpdRect clipped(EpdRect r,EpdRect body) {
 int right=r.x+r.width,bottom=r.y+r.height;if(r.x<body.x)r.x=body.x;if(r.y<body.y)r.y=body.y;
 if(right>body.x+body.width)right=body.x+body.width;
 if(bottom>body.y+body.height)bottom=body.y+body.height;
 r.width=right>r.x?right-r.x:0;r.height=bottom>r.y?bottom-r.y:0;return r;
}
static void underline(uint8_t *fb,EpdRect r,EpdRect body,uint8_t gray,int height) {
 r=(EpdRect){r.x,r.y+r.height-1,r.width,height};r=clipped(r,(EpdRect){body.x,body.y,body.width,body.height+3});
 if(r.width && r.height)epd_fill_rect(r,gray,fb);
}
// 框只压深背景，不覆盖字形灰阶；先旋转整段，避免逐点调用坐标转换。
// Borders darken the background without washing out glyphs; rotate whole segments instead of every pixel.
static void darken_border(uint8_t *fb,EpdRect r,uint8_t gray) {
 r=clipped(r,h.body);if(r.width<=0 || r.height<=0)return;
 EpdRect physical=ui_rotate_rect_to_fb(r);unsigned value=gray>>4;
 for(int y=physical.y;y<physical.y+physical.height;y++)for(int x=physical.x;x<physical.x+physical.width;x++) {
  size_t offset=(size_t)y*epd_width()/2+(size_t)x/2;unsigned shift=(unsigned)(x&1)*4;
  unsigned current=(fb[offset]>>shift)&15;
  if(current>value)fb[offset]=(uint8_t)((fb[offset]&~(15u<<shift))|(value<<shift));
 }
}
static void sentence_outline(uint8_t *fb,EpdRect r,bool selected) {
 if(r.width<=0 || r.height<=0)return;
 // 每行按句子的原字形横界收框，左右不延伸到相邻句。
 // Keep each line's horizontal ink bounds inside its sentence, never across a neighbor.
 r.y-=4;r.height+=8;r=clipped(r,h.body);
 uint8_t gray=selected?UI_GRAY_BLACK:0x80;
 int horizontal=selected?3:1,vertical=selected?2:1;
 if(horizontal>r.height)horizontal=r.height;
 if(vertical>r.width)vertical=r.width;
 darken_border(fb,(EpdRect){r.x,r.y,r.width,horizontal},gray);
 darken_border(fb,(EpdRect){r.x,r.y+r.height-horizontal,r.width,horizontal},gray);
 darken_border(fb,(EpdRect){r.x,r.y,vertical,r.height},gray);
 darken_border(fb,(EpdRect){r.x+r.width-vertical,r.y,vertical,r.height},gray);
}
void reader_highlight_ui_draw(uint8_t *fb) {
 if(!h.active || !fb)return;
 memcpy(fb,h.snapshot,h.frame_bytes);
 EpdRect row={0};size_t sentence=SIZE_MAX;bool selected=false;
 for(size_t i=0;i<h.n;i++) {
  const mark_cell_t *cell=&h.cells[i];
  if(row.width && (row.y!=cell->rect.y || sentence!=cell->sentence_first)) {
   sentence_outline(fb,row,selected);row=(EpdRect){0};
  }
  if(!row.width) {
   sentence=cell->sentence_first;
   selected=h.selected && cell->sentence_end>h.first && cell->sentence_first<h.last;
  }
  row=row.width?ui_rect_union(row,cell->rect):cell->rect;
 }
 if(row.width)sentence_outline(fb,row,selected);
 ui_clear_rect_fast(fb,(EpdRect){0,h.footer,UI_LOCK_WIDTH,UI_LOCK_HEIGHT-h.footer});
 ui_hairline(fb,h.footer,30,UI_LOCK_WIDTH-60,0xc0);
 ui_text_vc(fb,UI_LOCK_WIDTH/6,h.footer+42,28,"取消",EPD_DRAW_ALIGN_CENTER,false);
 if(h.message)ui_text_vc(fb,UI_LOCK_WIDTH/2,h.footer+42,22,h.message,EPD_DRAW_ALIGN_CENTER,false);
 if(h.selected)ui_text_vc(fb,UI_LOCK_WIDTH*5/6,h.footer+42,28,selection_is_marked()?"取消划线":"划线",EPD_DRAW_ALIGN_CENTER,false);
}
// 已保存线使用两像素纯黑，避免默认 DU 将浅灰目标归到白色而消失。
// Saved two-pixel black strokes survive default DU instead of mapping light gray targets to white.
typedef struct { uint8_t *fb;EpdRect body,row;size_t index; } draw_mark_t;
static bool draw_mark(const book_layout_glyph_t *g,void *context) {
 draw_mark_t *m=context;
 while(m->index<h.mark_count && h.marks[m->index].end<=g->start)m->index++;
 bool selected=m->index<h.mark_count && h.marks[m->index].start<g->end;
 if(m->row.width && (!selected || m->row.y!=g->rect.y)){underline(m->fb,m->row,m->body,UI_GRAY_BLACK,2);m->row=(EpdRect){0};}
 if(selected)m->row=m->row.width?ui_rect_union(m->row,g->rect):g->rect;
 return true;
}
void reader_highlight_ui_marks(uint8_t *fb,size_t page,EpdRect body,int px) {
 if(!h.text || !h.mark_count)return;
 size_t first=book_layout_page_start_offset(page),end=book_layout_page_start_offset(page+1),i=0;
 while(i<h.mark_count && h.marks[i].end<=first)i++;
 if(i==h.mark_count || h.marks[i].start>=end)return;
 // 原页几何只遍历一次，不为每条划线重复排版或度量。/ Visit original geometry once, never once per highlight.
 draw_mark_t ctx={.fb=fb,.body=body,.index=i};
 (void)book_layout_visit_page(page,body,px,draw_mark,&ctx);
 if(ctx.row.width)underline(fb,ctx.row,body,UI_GRAY_BLACK,2);
}
