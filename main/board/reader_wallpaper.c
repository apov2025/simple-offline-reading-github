/* SPDX-License-Identifier: Apache-2.0
 * 小纸原生壁纸：单缓存、分步预读、成功显示后才保存轮换位置。
 * Native Pico wallpapers: one cache, incremental preload, rotation saved only after successful display.
 */
#include "reader_wallpaper.h"
#include "reader_media.h"
#include "reader_usb.h"
#include "settings.h"
#include "reader_wallpaper_badge.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif
#ifndef READER_WALLPAPER_ROOT
#define READER_WALLPAPER_ROOT "/sdcard/wallpapers"
#endif

enum { WP_FILES=64,WP_SCAN=256,WP_CHUNK=8192 };
typedef enum { WP_IDLE,WP_SCAN_DIR,WP_SELECT,WP_READ,WP_READY,WP_EMPTY } wallpaper_phase_t;
static struct {
    bool kind_set,off,borrowed;
    wallpaper_phase_t phase;
    DIR *dir;
    FILE *file;
    uint8_t *pixels;
    char names[WP_FILES][READER_WALLPAPER_NAME_BYTES];
    char selected[READER_WALLPAPER_NAME_BYTES];
    size_t count,scanned,next,tried,loaded;
} w;

void reader_wallpaper_invalidate(void) {
    if(!w.kind_set && !w.file && !w.dir && !w.pixels)return;
    if(w.file)(void)fclose(w.file);
    if(w.dir)(void)closedir(w.dir);
    free(w.pixels);memset(&w,0,sizeof(w));
}
static bool media_ready(void) {
    if(reader_usb_active())return false;
    reader_media_info_t info={0};
    return reader_media_get_info(&info)==ESP_OK && info.present && info.mounted;
}
static void abandon_file(void) {
    if(w.file)(void)fclose(w.file);
    w.file=NULL;w.loaded=0;w.phase=WP_SELECT;
}
static void exhausted(void) {
    free(w.pixels);w.pixels=NULL;w.phase=WP_EMPTY;
}
static int compare_names(const void *a,const void *b) { return strcmp(a,b); }
static void finish_scan(bool valid) {
    if(closedir(w.dir)!=0)valid=false;
    w.dir=NULL;
    if(!valid || !w.count) { exhausted();return; }
    qsort(w.names,w.count,sizeof(w.names[0]),compare_names);
    const char *last=app_settings_wallpaper_last(w.off);
    w.next=0;
    for(size_t i=0;i<w.count;i++)if(strcmp(w.names[i],last)>0) { w.next=i;break; }
    w.phase=WP_SELECT;
}
static void native_pixel(int x,int y,uint8_t gray) {
    size_t at=((size_t)y*684+(unsigned)x)/2;
    uint8_t old=w.pixels[at];
    w.pixels[at]=(x&1)?(uint8_t)((old&15)|(gray<<4)):(uint8_t)((old&240)|gray);
}
static void stamp_off(void) {
    // 固定的白底关机标记直接写入原生图，不在屏幕上电后访问字体。/ Stamp the fixed white shutdown badge into native pixels without any font access after display power-up.
    for(int y=0;y<48;y++)for(int x=0;x<140;x++) {
        int dx=x<12?12-x:x>=128?x-127:0;
        int dy=y<12?12-y:y>=36?y-35:0;
        if(dx*dx+dy*dy<=144)native_pixel(272+x,1144+y,15);
    }
    int left=272+(140-WP_BADGE_WIDTH)/2,top=1144+(48-WP_BADGE_HEIGHT)/2;
    for(int y=0;y<WP_BADGE_HEIGHT;y++)for(int x=0;x<WP_BADGE_WIDTH;x++)
        if(wallpaper_badge_bits[y*((WP_BADGE_WIDTH+7)/8)+x/8]&(0x80u>>(x&7)))native_pixel(left+x,top+y,0);
}
void reader_wallpaper_idle(bool off,bool cover) {
    if(reader_usb_active()) { reader_wallpaper_invalidate();return; }
    if(cover && !off) { reader_wallpaper_invalidate();return; }
    if(w.kind_set && w.off!=off)reader_wallpaper_invalidate();
    // 完成或无图后只等主循环的介质失效通知，不重复轮询卡。/ Once ready or empty, wait for loop media invalidation rather than repeatedly polling the card.
    if(w.borrowed || w.phase==WP_READY || w.phase==WP_EMPTY)return;
    if(!media_ready()) { reader_wallpaper_invalidate();return; }
    w.kind_set=true;w.off=off;
    if(w.phase==WP_IDLE) {
        char path[96];snprintf(path,sizeof(path),"%s/%s",READER_WALLPAPER_ROOT,off?"off":"lock");
        w.dir=opendir(path);w.phase=w.dir?WP_SCAN_DIR:WP_EMPTY;return;
    }
    if(w.phase==WP_SCAN_DIR) {
        errno=0;struct dirent *entry=readdir(w.dir);
        if(!entry) { finish_scan(errno==0);return; }
        ++w.scanned;
        size_t n=strlen(entry->d_name);
        if(entry->d_name[0]!='.' && n>5 && n<READER_WALLPAPER_NAME_BYTES && !strcmp(entry->d_name+n-5,".4bpp"))
            memcpy(w.names[w.count++],entry->d_name,n+1);
        if(w.count==WP_FILES || w.scanned==WP_SCAN)finish_scan(true);
        return;
    }
    if(w.phase==WP_SELECT) {
        if(w.tried==w.count) { exhausted();return; }
        const char *name=w.names[w.next];w.next=(w.next+1)%w.count;++w.tried;
        char path[160];snprintf(path,sizeof(path),"%s/%s/%s",READER_WALLPAPER_ROOT,off?"off":"lock",name);
        w.file=fopen(path,"rb");if(!w.file)return;
        struct stat info;
        if(fstat(fileno(w.file),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size!=READER_WALLPAPER_BYTES) { abandon_file();return; }
        if(!w.pixels) {
#if defined(ESP_PLATFORM)
            w.pixels=heap_caps_malloc(READER_WALLPAPER_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
            w.pixels=malloc(READER_WALLPAPER_BYTES);
#endif
            if(!w.pixels) { abandon_file();exhausted();return; }
        }
        memcpy(w.selected,name,strlen(name)+1);w.loaded=0;w.phase=WP_READ;return;
    }
    size_t count=READER_WALLPAPER_BYTES-w.loaded;if(count>WP_CHUNK)count=WP_CHUNK;
    if(fread(w.pixels+w.loaded,1,count,w.file)!=count || ferror(w.file)) { abandon_file();return; }
    w.loaded+=count;
    if(w.loaded==READER_WALLPAPER_BYTES) {
        bool valid=fgetc(w.file)==EOF && !ferror(w.file);
        if(fclose(w.file)!=0)valid=false;
        w.file=NULL;
        if(!valid) { w.loaded=0;w.phase=WP_SELECT;return; }
        if(w.off)stamp_off();
        w.phase=WP_READY;
    }
}
const uint8_t *reader_wallpaper_acquire(bool off,bool cover) {
    if(!media_ready() || (cover && !off) || !w.kind_set || w.off!=off || w.phase!=WP_READY) {
        reader_wallpaper_invalidate();return NULL;
    }
    w.borrowed=true;return w.pixels;
}
void reader_wallpaper_finish(bool displayed) {
    if(displayed && w.borrowed)(void)app_settings_set_wallpaper_last(w.off,w.selected);
    reader_wallpaper_invalidate();
}
