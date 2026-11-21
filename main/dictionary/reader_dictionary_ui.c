/*
 * SPDX-License-Identifier: Apache-2.0
 * 原页选词与简明释义；只在用户发起查询时读词典。/ Original-page selection and concise definitions; read the dictionary only on a lookup action.
 * 冻结：正文必须来自开层时的像素快照；不得重排、扩大原页或改字体。
 * Frozen: Body pixels come from the opening snapshot; never reflow, expand the page or change its font.
 * 冻结：首次纵移保留原行以避让手指，稳定后按相对偏移换本页可见行；不在移动时查询或读字体。
 * Frozen: The first vertical escape retains its row; after settling, relative motion changes visible page rows, without lookup or font reads during movement.
 */
#include "reader_dictionary_ui.h"
#include "reader_dictionary.h"
#include "reader_vocabulary.h"
#include "book_layout.h"
#include "ui_kit.h"
#include "ttf_font.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif

enum { PAD=30, CARD_HEIGHT=620, MAX_SELECTION=4, CARD_CORNER_WIDTH=72, RESELECT_TARGET=48 };
enum { HOLD_CLEARANCE_PX=8, HOLD_STILL_PX=3, HOLD_STILL_MS=120, HOLD_ROW_HYST_PX=6 };
typedef struct { size_t first,end; EpdRect rect; bool unboxed; } cell_t;
typedef struct { size_t first,end;bool component; } lookup_item_t;
typedef struct { size_t count,hit,first,end; uint16_t x,y; bool direct,preview; EpdRect bounds; unsigned distance; int hit_line; } glyph_scan_t;
static EpdRect selection_box(cell_t c);
static void hold_begin(cell_t target,uint16_t x,uint16_t y,bool opened);
static void query(size_t a,size_t b,size_t count);
static char query_book[192];
void reader_dict_ui_set_book(const char *title) {
    size_t n=title?strlen(title):0;
    if(n>=sizeof(query_book)) { n=sizeof(query_book)-1;while(n && ((unsigned char)title[n]&0xc0)==0x80)--n; }
    if(n)memcpy(query_book,title,n);
    query_book[n]=0;
}
static struct {
    const char *text;
    size_t start,end,page;
    EpdRect body;
    int px;
    size_t selection[MAX_SELECTION],selected_count;
    cell_t *cells;
    size_t n,capacity;
    uint8_t *snapshot,*framebuffer;
    size_t frame_bytes;
    int footer;
    bool card;
    char query[256];
    lookup_item_t items[132];
    size_t item,item_count;
    const char *message;
    reader_dict_entry_t *entry,*related;
    size_t part;
    bool fallback,review,save_failed,holding,hold_opened;
    size_t hold_first,hold_end,hold_index;
    int hold_line,hold_origin_x,hold_center_x;
    int hold_origin_y,hold_center_y,hold_still_y,hold_pointer_y,hold_anchor_y;
    int64_t hold_still_ms;
    bool hold_clearance,hold_rows;
    EpdRect hold_dirty,hold_trail,hold_cue,hold_cue_right,hold_visible;
    reader_dict_status_t query_status;
} d;

static void *overlay_alloc(size_t bytes) {
#if defined(ESP_PLATFORM)
    void *p=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return p?p:malloc(bytes);
#else
    return malloc(bytes);
#endif
}
static size_t step(const char *s,size_t n) {
    size_t i=1;
    while(i<n && ((unsigned char)s[i]&0xc0)==0x80) ++i;
    return i;
}
static uint32_t source_codepoint(const char *s,size_t n) {
    const unsigned char *p=(const unsigned char *)s;
    if(n==0 || n>4)return 0;
    if(n==1)return p[0];
    uint32_t cp=p[0]&(0x7f>>n);
    for(size_t i=1;i<n;i++)cp=(cp<<6)|(p[i]&63);
    return cp;
}
static bool latin_base(const char *s,size_t n) {
    uint32_t cp=source_codepoint(s,n);
    return (cp<128 && isalnum((unsigned char)cp)) ||
           (cp>=0xc0 && cp<=0x24f && cp!=0xd7 && cp!=0xf7) || (cp>=0x1e00 && cp<=0x1eff);
}
static bool combining_accent(const char *s,size_t n) {
    uint32_t cp=source_codepoint(s,n);
    return cp>=0x300 && cp<=0x36f;
}
static size_t semantic_hyphen(const char *s,size_t n) {
    if(n && *s=='-')return 1;
    return n>=3 && (!memcmp(s,"‐",3) || !memcmp(s,"‑",3))?3:0;
}
static size_t joiner(const char *s,size_t n) {
    size_t hyphen=semantic_hyphen(s,n);if(hyphen)return hyphen;
    if(n && *s=='\'')return 1;
    if(n>=2 && !memcmp(s,"\xc2\xad",2))return 2;
    return n>=3 && !memcmp(s,"’",3)?3:0;
}
static size_t word_end(const char *s,size_t n,size_t i) {
    size_t k=step(s+i,n-i);
    if(!latin_base(s+i,k))return i+k;
    i+=k;
    while(i<n) {
        k=step(s+i,n-i);
        if(latin_base(s+i,k) || combining_accent(s+i,k)) { i+=k;continue; }
        size_t j=joiner(s+i,n-i);
        if(!j || i+j>=n)break;
        k=step(s+i+j,n-i-j);
        if(!latin_base(s+i+j,k))break;
        i+=j+k;
    }
    return i;
}
// 只精确匹配常见功能词，不影响包含这些字母的长词或复合词。
// Match whole common function words only, never substrings or compound components.
static bool common_english_word(const char *s,size_t n) {
    static const char *const words[]={
        "a","an","the","i","you","he","she","it","we","they",
        "me","him","her","us","them","of","in","on","at","by",
        "to","for","from","with","under","and","or"
    };
    if(!n || n>5)return false;
    char key[6];
    for(size_t i=0;i<n;i++) {
        unsigned char ch=(unsigned char)s[i];
        if(ch>='A' && ch<='Z')ch+=(unsigned char)('a'-'A');
        if(ch<'a' || ch>'z')return false;
        key[i]=(char)ch;
    }
    key[n]=0;
    for(size_t i=0;i<sizeof(words)/sizeof(*words);i++)if(!strcmp(key,words[i]))return true;
    return false;
}
static bool han(const char *s,size_t n);
static bool count_glyph(const book_layout_glyph_t *glyph,void *context) {
    glyph_scan_t *scan=context;
    if(scan->count==SIZE_MAX/sizeof(cell_t))return false;
    if(scan->direct) {
        EpdRect r=glyph->rect;
        int left=r.x>scan->bounds.x?r.x:scan->bounds.x,top=r.y>scan->bounds.y?r.y:scan->bounds.y;
        int right=r.x+r.width<scan->bounds.x+scan->bounds.width?r.x+r.width:scan->bounds.x+scan->bounds.width;
        int bottom=r.y+r.height<scan->bounds.y+scan->bounds.height?r.y+r.height:scan->bounds.y+scan->bounds.height;
        if(right>left && bottom>top) {
            int dx=scan->x<left?left-scan->x:scan->x>=right?scan->x-right+1:0;
            int dy=scan->y<top?top-scan->y:scan->y>=bottom?scan->y-bottom+1:0;
            unsigned distance=(unsigned)(dx*dx+dy*dy);
            int radius=glyph->px/4;if(radius<12)radius=12;if(radius>18)radius=18;
            // 墨迹间隙允许小范围吸附；距离相同取阅读顺序靠前的字形，标点仍阻挡误选。
            // Allow bounded snapping across ink gaps; ties follow reading order and punctuation still blocks accidental word selection.
            if(distance<=(unsigned)(radius*radius) && (scan->hit==SIZE_MAX || distance<scan->distance)) {
                scan->hit=scan->count;scan->first=glyph->start;scan->end=glyph->end;scan->distance=distance;scan->hit_line=glyph->rect.y;
            }
        }
    }
    ++scan->count;
    return true;
}
static bool retain_glyph(const book_layout_glyph_t *glyph,void *context) {
    (void)context;
    if(d.n>=d.capacity || glyph->start>=glyph->end || glyph->end>d.end)return false;
    d.cells[d.n++]=(cell_t){glyph->start,glyph->end,glyph->rect,false};
    return true;
}
void reader_dict_ui_close(void) {
    if(d.snapshot && d.framebuffer)memcpy(d.framebuffer,d.snapshot,d.frame_bytes);
    free(d.entry);free(d.related);free(d.cells);free(d.snapshot);memset(&d,0,sizeof(d));
}
bool reader_dict_ui_active(void) { return d.text!=NULL; }
bool reader_dict_ui_selecting(void) { return d.text!=NULL && !d.card && !d.holding; }
bool reader_dict_ui_holding(void) { return d.holding; }
// 只比较空间线索的相对分数，不是训练模型或校准概率；不读词频、词义或历史。/ Relative spatial scores only, not a trained model or calibrated probability; no frequency, meaning or history reads.
static bool candidate_score(cell_t candidate,uint16_t x,uint16_t y,EpdRect visible,int *score) {
    EpdRect r=candidate.rect;
    int em=r.height;
    if(em<=0)return false;
    int left=r.x>visible.x?r.x:visible.x,top=r.y>visible.y?r.y:visible.y;
    int right=r.x+r.width<visible.x+visible.width?r.x+r.width:visible.x+visible.width;
    int bottom=r.y+r.height<visible.y+visible.height?r.y+r.height:visible.y+visible.height;
    if(right<=left || bottom<=top)return false;
    int dx=x<left?left-x:x>=right?x-right+1:0;
    int dy=y<top?top-y:y>=bottom?y-bottom+1:0;
    int distance=dx*dx+dy*dy,radius=em/4;
    if(radius<12)radius=12;
    if(radius>18)radius=18;
    if(distance>radius*radius)return false;
    int center=2*(int)y-(2*r.y+r.height-1);if(center<0)center=-center;
    // 整词内部与横向覆盖加分；按字号归一的区域距离及偏离行中心扣分。/ Reward inside-word and horizontal coverage; penalize font-normalized region distance and line-center displacement.
    *score=2000+(dx==0 && dy==0?240:0)+(dx==0?80:0)
        -(int)((int64_t)16000*distance/((int64_t)em*em))-(int)((int64_t)160*center/em);
    return true;
}
static bool ranked_target(uint16_t x,uint16_t y,EpdRect visible,cell_t *target) {
    if(!ui_rect_hit(visible,x,y))return false;
    bool found=false;int best=0;
    // 跨行同词分别评价可见片段，取最高分；不把换行之间的空白并成大词框。/ Score visible wrapped-word segments independently and take the best; never merge inter-line blank space into a word rectangle.
    for(size_t i=0;i<d.n;i++) {
        int score;
        if(candidate_score(d.cells[i],x,y,visible,&score) &&
           (!found || score>best || (score==best && d.cells[i].first<target->first))) {
            *target=d.cells[i];best=score;found=true;
        }
    }
    return found;
}
static bool open_overlay(const char *s,size_t len,size_t page,EpdRect body,int px,uint8_t *fb,
                         int footer_top,glyph_scan_t scan) {
    reader_dict_ui_close();
    size_t start=book_layout_page_start_offset(page),end=book_layout_page_start_offset(page+1);
    scan.hit=SIZE_MAX;scan.bounds=body;
    if(scan.direct && !ui_rect_hit(body,scan.x,scan.y))return false;
    if(!s || !fb || start>=end || end>len || footer_top<body.y+body.height || footer_top>=UI_LOCK_HEIGHT ||
       !book_layout_visit_page(page,body,px,count_glyph,&scan) || !scan.count)return false;
    d.frame_bytes=(size_t)epd_width()*epd_height()/2;
    d.snapshot=overlay_alloc(d.frame_bytes);d.cells=overlay_alloc(scan.count*sizeof(*d.cells));
    if(!d.snapshot || !d.cells) { reader_dict_ui_close();return false; }
    d.start=start;d.end=end;d.capacity=scan.count;
    if(!book_layout_visit_page(page,body,px,retain_glyph,NULL) || d.n!=scan.count) {
        reader_dict_ui_close();return false;
    }
    // 框只在原页，英文查询保留跨页完整词；人工换行连字符不进入原文。
    // Boxes stay on the original page; English lookup spans retain whole words across page edges, excluding inserted hyphens.
    size_t hit=0,token_start=start;
    while(token_start) {
        size_t previous=token_start-1;
        while(previous && ((unsigned char)s[previous]&0xc0)==0x80)--previous;
        size_t bytes=token_start-previous;
        if(!latin_base(s+previous,bytes) && !combining_accent(s+previous,bytes) && !joiner(s+previous,bytes))break;
        token_start=previous;
    }
    for(size_t i=token_start;i<end;) {
        size_t next=word_end(s,len,i);
        bool latin=latin_base(s+i,step(s+i,len-i));
        while(hit<d.n && d.cells[hit].first<next) {
            if(latin) { d.cells[hit].first=i;d.cells[hit].end=next; }
            ++hit;
        }
        i=next;
    }
    // 精确字形命中优先，最近标点仍阻挡；其余触点在合并词区后比较。/ Exact glyph hits lead and nearest punctuation still blocks; compare other touches after word regions are joined.
    cell_t target={0};bool exact=false;
    if(scan.direct && scan.hit!=SIZE_MAX) {
        target=d.cells[scan.hit];
        const char *start=s+target.first;
        size_t bytes=target.end-target.first;
        if(!latin_base(start,step(start,bytes)) && !han(start,bytes)) {
            reader_dict_ui_close();
            return false;
        }
        exact=scan.distance==0;
    }
    size_t boxes=0;
    for(size_t i=0;i<d.n;i++) {
        cell_t c=d.cells[i];
        const char *start=s+c.first;
        size_t bytes=c.end-c.first;
        if(!latin_base(start,step(start,bytes)) && !han(start,bytes))continue;
        if(boxes && d.cells[boxes-1].first==c.first && d.cells[boxes-1].rect.y==c.rect.y) {
            d.cells[boxes-1].rect=ui_rect_union(d.cells[boxes-1].rect,c.rect);
        } else d.cells[boxes++]=c;
    }
    d.n=boxes;
    for(size_t i=0;i<d.n;i++)d.cells[i].unboxed=false;
    if(!boxes || (scan.direct && !exact && !ranked_target(scan.x,scan.y,body,&target))) { reader_dict_ui_close();return false; }
    memcpy(d.snapshot,fb,d.frame_bytes);
    d.framebuffer=fb;d.text=s;d.footer=footer_top;
    d.page=page;d.body=body;d.px=px;
    if(scan.direct) {
        if(scan.preview)hold_begin(target,scan.x,scan.y,true);
        else query(target.first,target.end,1);
    }
    return true;
}
bool reader_dict_ui_open(const char *s,size_t len,size_t page,EpdRect body,int px,uint8_t *fb,int footer_top) {
    return open_overlay(s,len,page,body,px,fb,footer_top,(glyph_scan_t){0});
}
bool reader_dict_ui_open_at(const char *s,size_t len,size_t page,EpdRect body,int px,uint8_t *fb,
                            int footer_top,uint16_t x,uint16_t y) {
    return open_overlay(s,len,page,body,px,fb,footer_top,(glyph_scan_t){.x=x,.y=y,.direct=true});
}
bool reader_dict_ui_hold_at(const char *s,size_t len,size_t page,EpdRect body,int px,uint8_t *fb,
                            int footer_top,uint16_t x,uint16_t y) {
    return open_overlay(s,len,page,body,px,fb,footer_top,(glyph_scan_t){.x=x,.y=y,.direct=true,.preview=true});
}
bool reader_dict_ui_hold_finish(void) {
    if(!d.holding)return false;
    size_t first=d.hold_first,end=d.hold_end;
    d.holding=false;d.hold_opened=false;d.selected_count=0;query(first,end,1);return true;
}
bool reader_dict_ui_hold_cancel(void) {
    if(!d.holding)return false;
    bool close=d.hold_opened;d.holding=false;d.hold_opened=false;
    if(close)reader_dict_ui_close();
    return true;
}
static bool readable_pronunciation(const char *s) {
    size_t len=strlen(s);
    for(size_t i=0;i<len;) {
        size_t k=step(s+i,len-i);
        if(k>4 || !ttf_font_has_glyph(source_codepoint(s+i,k)))return false;
        i+=k;
    }
    return true;
}
static bool han(const char *s,size_t n) {
    if(n<3 || n>4)return false;
    uint32_t cp=source_codepoint(s,n);
    return (cp>=0x3400 && cp<=0x9fff) || (cp>=0x20000 && cp<=0x323af);
}
static bool source_key(size_t start,size_t end,char *out,size_t cap) {
    size_t w=0;
    for(size_t i=start;i<end;) {
        if(isspace((unsigned char)d.text[i])) {
            while(i<end && isspace((unsigned char)d.text[i]))++i;
            if(w+1>=cap) { out[w]=0;return false; }
            out[w++]=' ';
        } else {
            size_t k=step(d.text+i,end-i),hyphen=semantic_hyphen(d.text+i,end-i);
            if(k==2 && !memcmp(d.text+i,"\xc2\xad",2)) { i+=k;continue; }
            if(w+(hyphen?1:k)>=cap) { out[w]=0;return false; }
            if(hyphen)out[w++]='-';
            else { memcpy(out+w,d.text+i,k);w+=k; }
            i+=k;
        }
    }
    out[w]=0;return true;
}
static void lookup(reader_dict_session_t *session) {
    char key[256];
    bool fits=source_key(d.items[d.item].first,d.items[d.item].end,key,sizeof(key));
    d.part=0;d.message=NULL;
    if(!d.entry)d.entry=calloc(1,sizeof(*d.entry));
    if(!d.entry) { if(!d.item)d.query_status=READER_DICT_IO_ERROR;d.message="内存不足，请返回";return; }
    reader_dict_status_t status=fits?(session?reader_dictionary_session_lookup(session,key,d.entry):reader_dictionary_lookup(key,d.entry)):READER_DICT_NOT_FOUND;
    if(!d.item)d.query_status=status;
    if(status==READER_DICT_NOT_FOUND && !d.item && d.item_count>1) {
        d.fallback=true;d.item=1;lookup(session);return;
    }
    if(status==READER_DICT_OK && !d.item) {
        // 完整词组有释义时不拆普通词；语义连字符的组成词始终保留入口。
        // A known phrase keeps its own meaning; semantic-hyphen components always remain accessible.
        size_t w=1;
        for(size_t i=1;i<d.item_count;i++)if(d.items[i].component)d.items[w++]=d.items[i];
        d.item_count=w;
    }
    switch(status) {
        case READER_DICT_OK:return;
        case READER_DICT_NOT_FOUND:d.message="未收录";return;
        case READER_DICT_MISSING:d.message="未找到词典";return;
        case READER_DICT_INVALID:d.message="词典文件损坏";return;
        default:d.message="无法读取词典";return;
    }
}
static size_t selection_index(size_t first) {
    for(size_t i=0;i<d.selected_count;i++)if(d.cells[d.selection[i]].first==first)return i;
    return d.selected_count;
}
// 在原文验证连续选区，也计入区间中的未描框词。/ Validate continuity against source text and count unboxed words inside the selection.
static void confirm_selection(void) {
    cell_t initial=d.cells[d.selection[0]];
    size_t a=initial.first,b=initial.end,count=0;
    for(size_t i=1;i<d.selected_count;i++) {
        cell_t c=d.cells[d.selection[i]];
        if(c.first<a)a=c.first;
        if(c.end>b)b=c.end;
    }
    for(size_t i=a;i<b;) {
        size_t k=step(d.text+i,b-i);
        uint32_t cp=source_codepoint(d.text+i,k);
        if((cp<128 && isspace((unsigned char)cp)) || cp==0xa0 || cp==0x3000) { i+=k;continue; }
        size_t next=word_end(d.text,b,i);
        bool english=latin_base(d.text+i,k);
        if((!english && !han(d.text+i,next-i)) ||
           (selection_index(i)==d.selected_count && !(english && common_english_word(d.text+i,next-i)))) {
            d.message="请选相邻字词";
            return;
        }
        if(++count>MAX_SELECTION) {
            d.message="最多选择四个字词";
            return;
        }
        i=next;
    }
    query(a,b,count);
}
// 查询时只探测原页连续汉字里的一个最长相关词；绘图不访问词典。
// On query, find at most one longest related term in contiguous Han text on this page; drawing never reads the dictionary.
static void add_related_term(size_t a,size_t b,size_t selected_count,reader_dict_session_t *session) {
    if(selected_count<1 || selected_count>2 || a<d.start || b>d.end )return;
    size_t han_count=0;
    for(size_t i=a;i<b;) {
        size_t k=step(d.text+i,b-i);
        if(!han(d.text+i,k))return;
        i+=k;++han_count;
    }
    if(han_count!=selected_count)return;
    size_t left=a,right=b;
    for(int n=0;n<3 && left>d.start;n++) {
        size_t p=left-1;while(p>d.start && ((unsigned char)d.text[p]&0xc0)==0x80)--p;
        if(!han(d.text+p,left-p))break;
        left=p;
    }
    for(int n=0;n<3 && right<d.end;n++) {
        size_t k=step(d.text+right,d.end-right);
        if(!han(d.text+right,k))break;
        right+=k;
    }
    size_t offsets[9],n=0;
    for(size_t i=left;i<right;i+=step(d.text+i,right-i))offsets[n++]=i;
    offsets[n]=right;
    if(!d.entry)d.entry=calloc(1,sizeof(*d.entry));
    if(!d.entry)return;
    for(size_t length=4;length>=2;length--)for(size_t i=0;i+length<=n;i++) {
        size_t begin=offsets[i],end=offsets[i+length];
        if(begin>a || end<b || (begin==a && end==b))continue;
        char key[32];
        if(!source_key(begin,end,key,sizeof(key)))continue;
        reader_dict_status_t status=reader_dictionary_session_lookup(session,key,d.entry);
        if(status==READER_DICT_OK) {
            d.related=overlay_alloc(sizeof(*d.related));
            if(d.related)memcpy(d.related,d.entry,sizeof(*d.related));
            return;
        }
        if(status!=READER_DICT_NOT_FOUND)return;
    }
}
static void query(size_t a,size_t b,size_t count) {
    size_t previous=SIZE_MAX;
    free(d.related);d.related=NULL;
    bool fits=source_key(a,b,d.query,sizeof(d.query));
    d.item_count=1;d.item=0;d.items[0]=(lookup_item_t){a,b,false};
    for(size_t i=0;i<d.n;i++) {
        cell_t c=d.cells[i];
        if(c.first<a || c.end>b || c.first==previous)continue;
        previous=c.first;
        if(c.unboxed)continue;
        if(count>1 && d.item_count<132)d.items[d.item_count++]=(lookup_item_t){c.first,c.end,false};
        // 源文本连字符是复合词；人工换行连字符根本不在这些字节中。
        // Source hyphens form compounds; artificial wrapping hyphens never occur in these bytes.
        if(latin_base(d.text+c.first,step(d.text+c.first,c.end-c.first))) {
            size_t start=c.first;bool compound=false;
            for(size_t j=c.first;j<c.end;j+=step(d.text+j,c.end-j))if(semantic_hyphen(d.text+j,c.end-j))compound=true;
            if(compound)for(size_t j=c.first;j<=c.end && d.item_count<132;) {
                size_t hyphen=j<c.end?semantic_hyphen(d.text+j,c.end-j):0;
                if(j==c.end || hyphen) {
                    if(j>start)d.items[d.item_count++]=(lookup_item_t){start,j,true};
                    start=j+hyphen;
                }
                if(j==c.end)break;
                j+=step(d.text+j,c.end-j);
            }
        }
    }
#if defined(ESP_PLATFORM)
    const int64_t lookup_started=esp_timer_get_time();
#endif
    // Share one read-only file/map only within this synchronous action; release before saving or drawing.
    reader_dict_session_t session={0};
    reader_dictionary_session_open(&session,NULL);
    add_related_term(a,b,count,&session);
    d.card=true;d.fallback=false;lookup(&session);
    if(reader_dictionary_session_close(&session)!=READER_DICT_OK) {
        d.query_status=READER_DICT_IO_ERROR;d.message="无法读取词典";
        if(d.entry)memset(d.entry,0,sizeof(*d.entry));
        free(d.related);d.related=NULL;
    }
#if defined(ESP_PLATFORM)
    const int64_t lookup_finished=esp_timer_get_time();
#endif
    d.save_failed=!fits || !reader_vocabulary_record(d.query,query_book,d.query_status);
#if defined(ESP_PLATFORM)
    ESP_LOGI("reader_dict","perf query: lookup=%lld us save=%lld us",
        (long long)(lookup_finished-lookup_started),(long long)(esp_timer_get_time()-lookup_finished));
#endif
}
// 逐字测量且只在完整字符处续页。/ Measure text and continue pages only on UTF-8 boundaries.
static size_t wrap_line(const char *s,int px,int width,char *out,size_t cap) {
    size_t n=0,total=strlen(s);
    while(n<total && s[n]!='\n') {
        size_t k=step(s+n,total-n);
        if(n+k>=cap)break;
        memcpy(out+n,s+n,k);out[n+k]=0;
        if(n && ttf_text_width_px(px,out)>width)break;
        n+=k;
    }
    out[n]=0;return n+(s[n]=='\n');
}
// 只截取完整 UTF-8 字符，在最后一行明确省略；不改写源释义。
// Truncate only at UTF-8 boundaries and mark the final line; never rewrite source definitions.
static void add_ellipsis(char *text,size_t cap,int px,int width) {
    size_t n=strlen(text);
    while(n && (n+4>cap || ttf_text_width_px(px,text)+ttf_text_width_px(px,"…")>width)) {
        do { --n; } while(n && ((unsigned char)text[n]&0xc0)==0x80);
        text[n]=0;
    }
    if(n+4<=cap)strcat(text,"…");
}
static int compact_text(uint8_t *fb,int y,int px,const char *text,int max_lines) {
    char line[256];int used=0;
    for(int row=0;row<max_lines && *text;row++) {
        size_t k=wrap_line(text,px,UI_LOCK_WIDTH-2*PAD-CARD_CORNER_WIDTH,line,sizeof(line));
        if(!k)break;
        text+=k;
        if(row+1==max_lines && *text)add_ellipsis(line,sizeof(line),px,UI_LOCK_WIDTH-2*PAD-CARD_CORNER_WIDTH);
        if(fb)ui_text(fb,PAD,y+used,px,line,EPD_DRAW_ALIGN_LEFT,false);
        used+=px+8;
    }
    return used;
}
static void inline_fragment(char *out,size_t cap,const char *text,int width) {
    size_t used=wrap_line(text,30,width,out,cap);
    if(text[used])add_ellipsis(out,cap,30,width);
}
static bool entry_is_idiom(const reader_dict_entry_t *entry) {
    for(size_t i=0;i<entry->count;i++)if(strstr(entry->senses[i].pos,"成语"))return true;
    return false;
}
static void sense_excerpt(const reader_dict_entry_t *entry,size_t sense,bool numbered,char *out,size_t cap) {
    const reader_dict_sense_t *s=&entry->senses[sense];
    const char *pron=!entry_is_idiom(entry) && *s->pronunciation && strcmp(s->pronunciation,entry->pronunciation) && readable_pronunciation(s->pronunciation)?s->pronunciation:"";
    char prefix[16]="";
    if(numbered)snprintf(prefix,sizeof(prefix),"%zu. ",sense+1);
    snprintf(out,cap,"%s%s%s%s",prefix,pron,*pron?"  ":"",s->gloss);
}
static int compact_entry(uint8_t *fb,int y,const reader_dict_entry_t *entry,const char *title,const char *message,size_t limit) {
    char paragraph[4096],excerpt[1024],word[256],pron[256];
    inline_fragment(word,sizeof(word),title,240);
    inline_fragment(pron,sizeof(pron),!message && !entry_is_idiom(entry) && *entry->pronunciation && readable_pronunciation(entry->pronunciation)?entry->pronunciation:"",300);
    snprintf(paragraph,sizeof(paragraph),"%s%s%s%s",word,*pron?"（":"",pron,*pron?"）":"");
    size_t used=strlen(paragraph);
    if(message) {
        snprintf(paragraph+used,sizeof(paragraph)-used,"  %s",message);
        return compact_text(fb,y,30,paragraph,4);
    }
    // 按来源顺序保留前几义，只安全截短过长显示，不重新排序含义。
    // Keep the first senses in source order and only clip long display text; never reorder meanings.
    for(size_t i=0;i<entry->count && i<limit;i++) {
        sense_excerpt(entry,i,entry->count>1,excerpt,sizeof(excerpt));
        snprintf(paragraph+used,sizeof(paragraph)-used,"  %s",excerpt);
        used=strlen(paragraph);
    }
    return compact_text(fb,y,30,paragraph,4);
}
// 单屏短卡：原字词与完整词直接接排，不生成相关词入口或续页。
// One compact card directly combines the selected entry and complete term, without a related-term entry point or continuation pages.
static size_t card_lines(uint8_t *fb,int y,int *height) {
    int top=y;char title[256];
    source_key(d.items[d.item].first,d.items[d.item].end,title,sizeof(title));
    if(!d.message && *d.entry->word)snprintf(title,sizeof(title),"%s",d.entry->word);
    y+=compact_entry(fb,y,d.entry,title,d.message,d.related?2:3);
    if(d.related) {
        y+=12;
        y+=compact_entry(fb,y,d.related,d.related->word,NULL,2);
    }
    if(height)*height=y-top;
    return 1;
}
static bool move_item(int dir) {
    if(dir<0 && (d.item>1 || (d.item && !d.fallback)))d.item--;
    else if(dir>0 && d.item+1<d.item_count)d.item++;
    else return false;
    lookup(NULL);return true;
}
static EpdRect card_rect(void) {
    int height;card_lines(NULL,0,&height);
    int y=UI_LOCK_HEIGHT-(d.item_count>1?70:0)-20-height-24;
    return (EpdRect){0,y,UI_LOCK_WIDTH,UI_LOCK_HEIGHT-y};
}
// 角落保留独立触控区，正文行宽避开它；不再占用整行重选栏。/ Reserve a corner touch target outside the text width instead of a full reselect row.
static EpdRect reselect_rect(void) {
    EpdRect card=card_rect();
    return (EpdRect){UI_LOCK_WIDTH-PAD-RESELECT_TARGET,card.y+8,RESELECT_TARGET,RESELECT_TARGET};
}
static void draw_reselect_icon(uint8_t *fb) {
    EpdRect hit=reselect_rect();int x=hit.x+12,y=hit.y+12,size=24,arm=8,stroke=2;
    for(int i=0;i<2;i++)for(int j=0;j<2;j++) {
        int left=x+i*(size-arm),top=y+j*(size-stroke);
        epd_fill_rect((EpdRect){left,top,arm,stroke},UI_GRAY_BLACK,fb);
        left=x+i*(size-stroke);top=y+j*(size-arm);
        epd_fill_rect((EpdRect){left,top,stroke,arm},UI_GRAY_BLACK,fb);
    }
    epd_fill_rect((EpdRect){x+7,y+8,10,2},UI_GRAY_BLACK,fb);
    epd_fill_rect((EpdRect){x+7,y+14,10,2},UI_GRAY_BLACK,fb);
}
static bool visible_target(uint16_t x,uint16_t y,cell_t *target) {
    if(!reader_dict_ui_active() || d.review || !d.card || ui_rect_hit(card_rect(),x,y))return false;
    EpdRect visible=d.body;
    if(visible.y+visible.height>card_rect().y)visible.height=card_rect().y-visible.y;
    if(!ui_rect_hit(visible,x,y))return false;
    glyph_scan_t scan={.hit=SIZE_MAX,.x=x,.y=y,.direct=true,.bounds=visible};
    if(!book_layout_visit_page(d.page,d.body,d.px,count_glyph,&scan))return false;
    if(scan.hit!=SIZE_MAX) {
        bool readable=false;
        for(size_t i=0;i<d.n;i++) {
            cell_t c=d.cells[i];
            if(c.first<=scan.first && c.end>=scan.end && c.rect.y==scan.hit_line) {
                if(scan.distance==0) { *target=c;return true; }
                readable=true;break;
            }
        }
        if(!readable)return false;
    }
    return ranked_target(x,y,visible,target);
}
bool reader_dict_ui_query_at(uint16_t x,uint16_t y) {
    cell_t target;
    if(d.holding || !visible_target(x,y,&target))return false;
    d.selected_count=0;query(target.first,target.end,1);return true;
}
bool reader_dict_ui_hold_existing(uint16_t x,uint16_t y) {
    cell_t target;
    if(d.holding || !visible_target(x,y,&target))return false;
    hold_begin(target,x,y,false);return true;
}
static EpdRect hold_box(cell_t c) {
    EpdRect r=selection_box(c),visible=d.hold_visible;
    int left=r.x>visible.x?r.x:visible.x,top=r.y>visible.y?r.y:visible.y;
    int right=r.x+r.width<visible.x+visible.width?r.x+r.width:visible.x+visible.width;
    int bottom=r.y+r.height<visible.y+visible.height?r.y+r.height:visible.y+visible.height;
    return (EpdRect){left,top,right>left?right-left:0,bottom>top?bottom-top:0};
}
static void hold_cues(cell_t target) {
    int top=target.rect.y>d.body.y?target.rect.y:d.body.y,bottom=target.rect.y+target.rect.height;
    int visible_bottom=d.hold_visible.y+d.hold_visible.height;
    if(bottom>visible_bottom)bottom=visible_bottom;
    d.hold_cue=d.hold_cue_right=(EpdRect){0};
    if(d.body.x>=14 && bottom>top) {
        int radius=bottom-top<7?(bottom-top-1)/2:3;
        int cy=target.rect.y+target.rect.height/2;
        if(cy<top+radius)cy=top+radius;
        if(cy>=bottom-radius)cy=bottom-radius-1;
        d.hold_cue=(EpdRect){d.body.x-10-radius,cy-radius,2*radius+1,2*radius+1};
    }
    if(d.hold_cue.width && d.body.x+d.body.width+14<=UI_LOCK_WIDTH)d.hold_cue_right=(EpdRect){d.body.x+d.body.width+10-d.hold_cue.width/2,d.hold_cue.y,d.hold_cue.width,d.hold_cue.height};
}
static void hold_begin(cell_t target,uint16_t x,uint16_t y,bool opened) {
    // 精确命中可能只给单个字母；锁定其整词在当前行的已合并片段。/ Exact hits may identify one letter; lock its merged whole-word segment on the current line.
    d.hold_index=0;
    for(size_t i=0;i<d.n;i++)if(d.cells[i].first==target.first && d.cells[i].end==target.end && d.cells[i].rect.y==target.rect.y) {
        d.hold_index=i;target=d.cells[i];break;
    }
    // 本次长按期间原卡不变，裁剪边界只量一次；移动只算缓存矩形。/ The original card stays fixed during a hold: measure its clip once, then move using cached rectangles only.
    d.hold_visible=d.body;
    if(d.card) {
        int bottom=card_rect().y;
        if(d.hold_visible.y+d.hold_visible.height>bottom)d.hold_visible.height=bottom-d.hold_visible.y;
    }
    d.holding=true;d.hold_opened=opened;d.hold_first=target.first;d.hold_end=target.end;
    d.hold_line=target.rect.y;d.hold_origin_x=x;d.hold_center_x=target.rect.x+target.rect.width/2;
    d.hold_origin_y=d.hold_pointer_y=d.hold_still_y=d.hold_anchor_y=y;
    d.hold_center_y=target.rect.y+target.rect.height/2;
    d.hold_clearance=d.hold_rows=false;d.hold_still_ms=0;
    EpdRect box=hold_box(target);
    hold_cues(target);
    d.hold_dirty=d.hold_cue.width?ui_rect_union(box,d.hold_cue):box;
    if(d.hold_cue_right.width)d.hold_dirty=ui_rect_union(d.hold_dirty,d.hold_cue_right);
    d.hold_trail=d.hold_dirty;
}
void reader_dict_ui_hold_tick(int64_t now_ms) {
    // 必须先观察到纵移再等待稳定；同轮较早的ctx时刻不会提前放开。
    // Observe a vertical escape before waiting for stability; an earlier ctx timestamp from this tick cannot arm prematurely.
    if(d.holding && d.hold_clearance && !d.hold_rows && now_ms>=d.hold_still_ms && now_ms-d.hold_still_ms>=HOLD_STILL_MS) {
        d.hold_anchor_y=d.hold_pointer_y;d.hold_rows=true;
    }
}
bool reader_dict_ui_hold_move(uint16_t x,uint16_t y) {
    if(!d.holding)return false;
    if(!d.hold_rows) {
        int delta=(int)y-(d.hold_clearance?d.hold_still_y:d.hold_origin_y);
        int threshold=d.hold_clearance?HOLD_STILL_PX+1:HOLD_CLEARANCE_PX;
        if(abs(delta)>=threshold) {
            d.hold_clearance=true;d.hold_still_y=y;d.hold_still_ms=esp_timer_get_time()/1000;
        }
    }
    d.hold_pointer_y=y;
    int target_line=d.hold_line;
    if(d.hold_rows) {
        int projected_y=d.hold_center_y+(int)y-d.hold_anchor_y;
        int current_center=d.cells[d.hold_index].rect.y+d.cells[d.hold_index].rect.height/2;
        int current_distance=abs(projected_y-current_center),nearest=current_distance;
        // 遍历本页缓存可见字形，不命中词卡后面或页面以外的行。
        // Search cached visible page glyphs only, excluding rows behind the card or outside this page.
        for(size_t i=0;i<d.n;i++) {
            cell_t c=d.cells[i];EpdRect visible=hold_box(c);
            if(visible.width<=0 || visible.height<=0)continue;
            int distance=abs(projected_y-(c.rect.y+c.rect.height/2));
            if(distance<nearest && distance+2*HOLD_ROW_HYST_PX<current_distance) { nearest=distance;target_line=c.rect.y; }
        }
    }
    int projected=d.hold_center_x+(int)x-d.hold_origin_x,best_distance=INT_MAX;
    size_t best=d.hold_index;
    // 选中行上空白/标点不是候选；超出行首尾时取最近可读目标。/ Spaces and punctuation are not candidates on the chosen row; outside its ends use the nearest readable target.
    for(size_t i=0;i<d.n;i++) {
        cell_t c=d.cells[i];if(c.rect.y!=target_line)continue;
        EpdRect visible=hold_box(c);if(visible.width<=0 || visible.height<=0)continue;
        int left=c.rect.x,right=c.rect.x+c.rect.width-1;
        if(left<d.body.x)left=d.body.x;
        if(right>=d.body.x+d.body.width)right=d.body.x+d.body.width-1;
        if(left>right)continue;
        int distance=projected<left?left-projected:projected>right?projected-right:0;
        if(distance<best_distance || (distance==best_distance && c.first<d.cells[best].first)) {
            best=i;best_distance=distance;
        }
    }
    if(best==d.hold_index)return false;
    EpdRect old=hold_box(d.cells[d.hold_index]),next=hold_box(d.cells[best]);
    d.hold_dirty=ui_rect_union(old,next);
    if(d.hold_cue.width)d.hold_dirty=ui_rect_union(d.hold_dirty,d.hold_cue);
    if(d.hold_cue_right.width)d.hold_dirty=ui_rect_union(d.hold_dirty,d.hold_cue_right);
    d.hold_index=best;d.hold_first=d.cells[best].first;d.hold_end=d.cells[best].end;d.hold_line=d.cells[best].rect.y;
    hold_cues(d.cells[best]);
    if(d.hold_cue.width)d.hold_dirty=ui_rect_union(d.hold_dirty,d.hold_cue);
    if(d.hold_cue_right.width)d.hold_dirty=ui_rect_union(d.hold_dirty,d.hold_cue_right);
    d.hold_trail=ui_rect_union(d.hold_trail,d.hold_dirty);
    return true;
}
EpdRect reader_dict_ui_hold_area(void) { return d.holding?d.hold_dirty:(EpdRect){0}; }
EpdRect reader_dict_ui_hold_restore_area(void) { return d.holding?d.hold_trail:(EpdRect){0}; }
static void draw_hold(uint8_t *fb) {
    // 跨行英文只描锁定行的片段，查询仍使用完整词的源跨度。/ Frame only the locked-line segment of a wrapped word; its query retains the complete source span.
    EpdRect r=hold_box(d.cells[d.hold_index]);
    if(r.width>0 && r.height>0)epd_draw_rect(r,UI_GRAY_BLACK,fb);
    if(d.hold_cue.width) {
        int radius=d.hold_cue.width/2;
        epd_fill_circle(d.hold_cue.x+radius,d.hold_cue.y+radius,radius,UI_GRAY_BLACK,fb);
        if(d.hold_cue_right.width)epd_fill_circle(d.hold_cue_right.x+radius,d.hold_cue_right.y+radius,radius,UI_GRAY_BLACK,fb);
    }
}

bool reader_dict_ui_tap(uint16_t x,uint16_t y) {
    if(!reader_dict_ui_active() || d.holding)return false;
    if(d.card) {
        if(!ui_rect_hit(card_rect(),x,y)) { reader_dict_ui_close();return true; }
        if(!d.review && ui_rect_hit(reselect_rect(),x,y)) {
            d.card=false;d.selected_count=0;d.message=NULL;
            free(d.entry);d.entry=NULL;
            free(d.related);d.related=NULL;
            return true;
        }
        if(d.item_count>1 && y>=UI_LOCK_HEIGHT-70)return move_item(x<UI_LOCK_WIDTH/2?-1:1);
        return false;
    }
    if(y>=d.footer) {
        if(x<UI_LOCK_WIDTH/3) { reader_dict_ui_close();return true; }
        if(x>=UI_LOCK_WIDTH*2/3 && d.selected_count) { confirm_selection();return true; }
        return false;
    }
    for(size_t i=0;i<d.n;i++) {
        cell_t c=d.cells[i];
        if(c.unboxed || !ui_rect_hit(selection_box(c),x,y))continue;
        d.message=NULL;
        size_t choice=selection_index(c.first);
        if(choice<d.selected_count)d.selection[choice]=d.selection[--d.selected_count];
        else if(d.selected_count<MAX_SELECTION)d.selection[d.selected_count++]=i;
        else d.message="最多选择四个字词";
        return true;
    }
    return false;
}
static void centered(uint8_t *fb,int x,int y,const char *s,int px) { ui_text_vc(fb,x,y,px,s,EPD_DRAW_ALIGN_CENTER,false); }
static EpdRect selection_box(cell_t c) {
    int x=c.rect.x-2,y=c.rect.y-2,w=c.rect.width+4,h=c.rect.height+4;
    if(x<0) { w+=x;x=0; }if(y<0) { h+=y;y=0; }
    if(x+w>UI_LOCK_WIDTH)w=UI_LOCK_WIDTH-x;
    if(y+h>d.footer)h=d.footer-y;
    return (EpdRect){x,y,w,h};
}
static void draw_selection_boxes(uint8_t *fb) {
    for(size_t i=0;i<d.n;i++) {
        if(d.cells[i].unboxed)continue;
        EpdRect r=selection_box(d.cells[i]);
        epd_draw_rect(r,UI_GRAY_BLACK,fb);
        if(selection_index(d.cells[i].first)<d.selected_count) {
            epd_draw_rect(ui_inset_rect(r,1),UI_GRAY_BLACK,fb);
            epd_draw_rect(ui_inset_rect(r,2),UI_GRAY_BLACK,fb);
        }
    }
}
void reader_dict_ui_draw(uint8_t *fb) {
    if(!reader_dict_ui_active() || !fb)return;
    memcpy(fb,d.snapshot,d.frame_bytes);
    if(d.card) {
        EpdRect card=card_rect();
        ui_clear_rect_fast(fb,card);
        ui_hairline(fb,card.y,PAD,UI_LOCK_WIDTH-2*PAD,0x50);
        card_lines(fb,card.y+20,NULL);
        if(d.item_count>1) {
            if(d.item>1 || (d.item && !d.fallback))centered(fb,UI_LOCK_WIDTH/4,UI_LOCK_HEIGHT-35,"上一项",28);
            if(d.item+1<d.item_count)centered(fb,UI_LOCK_WIDTH*3/4,UI_LOCK_HEIGHT-35,"下一项",28);
        }
        if(!d.review)draw_reselect_icon(fb);
        if(d.save_failed)ui_text(fb,UI_LOCK_WIDTH-PAD,card.y+58,18,"未保存",EPD_DRAW_ALIGN_RIGHT,false);
        if(d.holding)draw_hold(fb);
        return;
    }
    if(d.holding) { draw_hold(fb);return; }
    draw_selection_boxes(fb);
    ui_clear_rect_fast(fb,(EpdRect){0,d.footer,UI_LOCK_WIDTH,UI_LOCK_HEIGHT-d.footer});
    ui_hairline(fb,d.footer,PAD,UI_LOCK_WIDTH-2*PAD,0xc0);
    centered(fb,UI_LOCK_WIDTH/6,d.footer+42,"取消",28);
    centered(fb,UI_LOCK_WIDTH/2,d.footer+42,d.message?d.message:"点选相邻字词",22);
    if(d.selected_count)centered(fb,UI_LOCK_WIDTH*5/6,d.footer+42,"查询",28);
}

bool reader_dict_ui_review(const char *query,uint8_t *fb,int footer_top) {
    if(!query || !*query || strlen(query)>=sizeof(d.query) || !fb)return false;
    reader_dict_ui_close();
    d.frame_bytes=(size_t)epd_width()*epd_height()/2;
    d.snapshot=overlay_alloc(d.frame_bytes);
    if(!d.snapshot) { reader_dict_ui_close();return false; }
    memcpy(d.snapshot,fb,d.frame_bytes);d.framebuffer=fb;
    strcpy(d.query,query);d.text=d.query;d.footer=footer_top;
    d.items[0]=(lookup_item_t){0,strlen(d.query),false};d.item_count=1;
    d.review=true;d.card=true;lookup(NULL);return true;
}

