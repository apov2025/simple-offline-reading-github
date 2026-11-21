/* SPDX-License-Identifier: Apache-2.0
 * 主机复合字形回归：与未裁切的原字体逐像素比较。
 * Host compound-glyph regression: compare against the intact source font. */
#include <stdarg.h>
#include <time.h>
#include <stddef.h>
#include "../main/font/ttf_font.c"
const uint8_t builtin_ttf_start[1] asm("_binary_builtin_ttf_start")={0};
const uint8_t builtin_ttf_end[1] asm("_binary_builtin_ttf_end")={0};
static size_t live_bytes,peak_bytes;
typedef union {size_t n;max_align_t align;} allocation_header;
void* heap_caps_malloc(size_t n,uint32_t caps){(void)caps;allocation_header *p=malloc(n+sizeof(*p));if(!p)return NULL;p->n=n;live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;return p+1;}
void* heap_caps_calloc(size_t n,size_t s,uint32_t caps){void*p=heap_caps_malloc(n*s,caps);if(p)memset(p,0,n*s);return p;}
void heap_caps_free(void*p){if(!p)return;allocation_header*h=((allocation_header*)p)-1;live_bytes-=h->n;free(h);}
size_t heap_caps_get_largest_free_block(uint32_t caps){(void)caps;return 8*1024*1024-live_bytes;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void sim_log(esp_log_level_t level,const char*tag,const char*fmt,...){(void)level;fprintf(stderr,"%s: ",tag);va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);}
esp_err_t book_store_flash_mount(void){return ESP_FAIL;}
const char* app_settings_font_path(void){return "builtin";}

#include <assert.h>
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t*fb){(void)x;(void)y;(void)color;(void)fb;}
int main(int argc,char**argv){
 assert(argc>1);
 for(int a=1;a<argc;a++){
  FILE*f=fopen(argv[a],"rb");assert(f);fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
  unsigned char*raw=malloc((size_t)len);assert(raw);assert(fread(raw,1,(size_t)len,f)==(size_t)len);fclose(f);
  stbtt_fontinfo intact;assert(stbtt_InitFont(&intact,raw,0));
  assert(ttf_font_open(argv[a])==ESP_OK);
  // 原字体默认轮廓用于对照；此测试只覆盖轮廓打包。/ Use default outlines; this test covers packing only.
  ttf_set_weight(wght_def);
  const unsigned cps[]={0x41,0x42,0x43,0x44,0xbc,0x1ab8,0x4e00,0x9f98};
  unsigned checks=0;
  for(unsigned c=0;c<sizeof(cps)/sizeof(cps[0]);c++){
   unsigned cp=cps[c];if(!stbtt_FindGlyphIndex(&intact,(int)cp))continue;
   for(int px=36;px<=72;px+=12){
    int w,h,x,y;float scale=stbtt_ScaleForPixelHeight(&intact,(float)px);
    unsigned char*expected=stbtt_GetCodepointBitmap(&intact,scale,scale,(int)cp,&w,&h,&x,&y);
    fprintf(stderr,"checking %s U+%04X px=%d\n",argv[a],cp,px);
    const glyph_entry_t*actual=get_glyph(cp,px);assert(actual);
    assert(actual->width==w&&actual->height==h&&actual->left==x&&actual->top==-y);
    assert(!w||!h||memcmp(actual->bitmap,expected,(size_t)w*h)==0);
    assert(get_glyph(cp,px)==actual);
    stbtt_FreeBitmap(expected,NULL);checks++;
   }
  }
  printf("compound glyphs: %s %u exact raster/cache comparisons passed\n",argv[a],checks);
  ttf_font_unload();free(raw);
 }
 return 0;
}
