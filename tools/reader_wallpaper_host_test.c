/* SPDX-License-Identifier: Apache-2.0
 * 实际壁纸状态机的轮换与失败测试。/ Rotation and failure tests for the actual wallpaper state machine.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "reader_wallpaper.h"
#include "read_pico_sd.h"
static bool media=true,usb,fail_read,fail_close,fail_commit,fail_alloc;
static int files,dirs,reads,opens,scans,entries;
static char cursor[2][READER_WALLPAPER_NAME_BYTES];
bool reader_usb_active(void) { return usb; }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) { assert(!usb);*info=(read_pico_sd_info_t){media,media};return 0; }
const char *app_settings_wallpaper_last(bool off) { return cursor[off]; }
bool app_settings_set_wallpaper_last(bool off,const char *name) { if(fail_commit)return false;snprintf(cursor[off],sizeof(cursor[off]),"%s",name);return true; }
static FILE *test_open(const char *path,const char *mode) { assert(!usb && media);FILE *f=fopen(path,mode);++opens;if(f)++files;return f; }
static size_t test_read(void *p,size_t size,size_t n,FILE *f) { assert(!usb && media && size*n<=8192);++reads;return fail_read?0:fread(p,size,n,f); }
static int test_close(FILE *f) { --files;int r=fclose(f);return fail_close?EOF:r; }
static DIR *test_opendir(const char *path) { assert(!usb && media);++scans;DIR *d=opendir(path);if(d)++dirs;return d; }
static void *test_alloc(size_t n) { return fail_alloc?NULL:malloc(n); }
static struct dirent *test_readdir(DIR *d) { ++entries;return readdir(d); }
static int test_closedir(DIR *d) { --dirs;return closedir(d); }
#define READER_WALLPAPER_ROOT "wallpapers"
#define malloc test_alloc
#define readdir test_readdir
#define fopen test_open
#define fread test_read
#define fclose test_close
#define opendir test_opendir
#define closedir test_closedir
#include "../main/board/reader_wallpaper.c"
#undef malloc
#undef readdir
#undef fopen
#undef fread
#undef fclose
#undef opendir
#undef closedir
static void make_image(const char *path,size_t n,unsigned char value) {
    FILE *f=fopen(path,"wb");assert(f);unsigned char buf[4096];memset(buf,value,sizeof(buf));
    while(n) { size_t k=n<sizeof(buf)?n:sizeof(buf);assert(fwrite(buf,1,k,f)==k);n-=k; }assert(!fclose(f));
}
static void prepare(bool off) { for(int i=0;i<400;i++)reader_wallpaper_idle(off,false);assert(!files && !dirs); }
int main(void) {
    assert(!mkdir("wallpapers",0700) && !mkdir("wallpapers/lock",0700) && !mkdir("wallpapers/off",0700));
    make_image("wallpapers/lock/001.4bpp",READER_WALLPAPER_BYTES,0x21);
    make_image("wallpapers/lock/002.4bpp",17,0x33);
    make_image("wallpapers/lock/003.4bpp",READER_WALLPAPER_BYTES,0x65);
    make_image("wallpapers/off/001.4bpp",READER_WALLPAPER_BYTES,0xa9);
    prepare(false);const uint8_t *p=reader_wallpaper_acquire(false,false);assert(p && p[0]==0x21 && p[READER_WALLPAPER_BYTES-1]==0x21);
    assert(!*cursor[0]);reader_wallpaper_finish(true);assert(!strcmp(cursor[0],"001.4bpp"));
    prepare(false);p=reader_wallpaper_acquire(false,false);assert(p && p[0]==0x65);reader_wallpaper_finish(false);assert(!strcmp(cursor[0],"001.4bpp"));
    prepare(false);assert(!reader_wallpaper_acquire(false,true));reader_wallpaper_finish(true);assert(!strcmp(cursor[0],"001.4bpp"));
    prepare(false);p=reader_wallpaper_acquire(false,false);assert(p && p[0]==0x65);reader_wallpaper_finish(true);assert(!strcmp(cursor[0],"003.4bpp"));
    prepare(false);p=reader_wallpaper_acquire(false,false);assert(p && p[0]==0x21);reader_wallpaper_finish(true);
    fail_read=true;prepare(false);assert(!reader_wallpaper_acquire(false,false));assert(!files && !dirs);fail_read=false;
    fail_close=true;prepare(false);assert(!reader_wallpaper_acquire(false,false));fail_close=false;
    prepare(true);p=reader_wallpaper_acquire(true,false);assert(p && p[0]==0xa9 && p[READER_WALLPAPER_BYTES-1]==0xa9);
    size_t badge=(1144u*684u+272u)/2u;assert(memcmp(p+badge,(unsigned char[8]){0xa9,0xa9,0xa9,0xa9,0xa9,0xa9,0xa9,0xa9},8));
    const char *preview=getenv("WALLPAPER_TEST_PREVIEW");
    if(preview) { FILE *out=fopen(preview,"wb");assert(out && fwrite(p,1,READER_WALLPAPER_BYTES,out)==READER_WALLPAPER_BYTES && !fclose(out)); }
    reader_wallpaper_finish(true);assert(!strcmp(cursor[1],"001.4bpp"));
    prepare(false);int old_reads=reads;usb=true;assert(!reader_wallpaper_acquire(false,false));reader_wallpaper_idle(false,false);assert(reads==old_reads && !files && !dirs);usb=false;
    for(int i=0;i<10;i++)reader_wallpaper_idle(false,false);media=false;reader_wallpaper_invalidate();assert(!files && !dirs);assert(!reader_wallpaper_acquire(false,false));media=true;
    prepare(false);fail_commit=true;char saved[96];strcpy(saved,cursor[0]);assert(reader_wallpaper_acquire(false,false));reader_wallpaper_finish(true);assert(!strcmp(saved,cursor[0]));fail_commit=false;
    int prior_opens=opens;for(int i=0;i<100;i++)reader_wallpaper_idle(false,true);assert(opens==prior_opens);
    reader_wallpaper_invalidate();for(int i=0;i<100 && !files;i++)reader_wallpaper_idle(false,false);
    assert(files==1);assert(!reader_wallpaper_acquire(false,false) && !files && !dirs);
    fail_alloc=true;prepare(false);assert(!reader_wallpaper_acquire(false,false));fail_alloc=false;
    reader_wallpaper_invalidate();for(int i=0;i<100 && !files;i++)reader_wallpaper_idle(false,false);
    assert(files==1);reader_wallpaper_idle(true,false);assert(!files);
    prepare(true);assert(reader_wallpaper_acquire(true,false));reader_wallpaper_finish(false);
    assert(!unlink("wallpapers/lock/001.4bpp") && !unlink("wallpapers/lock/003.4bpp"));reader_wallpaper_invalidate();prepare(false);
    int old_scans=scans;for(int i=0;i<1000;i++)reader_wallpaper_idle(false,false);assert(scans==old_scans && !files && !dirs);
    assert(!reader_wallpaper_acquire(true,false));reader_wallpaper_finish(false);
    assert(!unlink("wallpapers/lock/002.4bpp"));
    for(int i=0;i<70;i++) { char name[80];snprintf(name,sizeof(name),"wallpapers/lock/cap%03d.4bpp",i);make_image(name,0,0); }
    reader_wallpaper_invalidate();prior_opens=opens;prepare(false);assert(opens-prior_opens==64);
    for(int i=0;i<70;i++) { char name[80];snprintf(name,sizeof(name),"wallpapers/lock/cap%03d.4bpp",i);assert(!unlink(name)); }
    for(int i=0;i<300;i++) { char name[80];snprintf(name,sizeof(name),"wallpapers/lock/.ignore%03d",i);make_image(name,0,0); }
    reader_wallpaper_invalidate();int prior_entries=entries;prepare(false);assert(entries-prior_entries==256 && !files && !dirs);
    puts("wallpaper: bounded scan/candidates, allocation/pending/mode cancellation, exact bytes, sorted persistent rotation, bad/read/close failures, cover/default non-consumption, mode/USB/media cancellation PASS");
}
