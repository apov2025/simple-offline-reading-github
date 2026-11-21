/* SPDX-License-Identifier: Apache-2.0
 * 两条小纸关机路径的预备/显示/提交顺序，不操作硬件。/ Preparation/display/commit order for both Pico shutdown paths, without hardware.
 */
#define app_settings_sleep_mode test_mode
#define reader_wallpaper_acquire test_acquire
#define reader_wallpaper_finish test_finish
#define reader_wallpaper_invalidate test_invalidate
#define epd_poweron test_poweron
#define epd_clear test_clear
#define epd_hl_set_all_white test_white
#define epd_hl_get_framebuffer test_framebuffer
#define epd_hl_update_screen_from_white test_from_white
#define update_display_full test_full
#define ui_draw_full_image test_image
#define ui_draw_power_screen test_power_screen
#include "../../main/sleep.c"
#include "../../main/apps/app_sleep.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static jmp_buf displayed;
static app_sleep_mode_t chosen;
static bool ready,commit_seen,fail_display;
static unsigned calls;
static char order[32];
static unsigned char pixels[684*1216/2],cover[2];
static const uint8_t *drawn;
static void mark(char c) { order[calls++]=c;order[calls]=0; }
app_sleep_mode_t test_mode(void) { return chosen; }
const uint8_t *test_acquire(bool off,bool has_cover) { assert(calls==0);mark('A');return ready && (off || !has_cover)?pixels:NULL; }
void test_finish(bool ok) { commit_seen=ok;mark('F');longjmp(displayed,1); }
void test_invalidate(void) { mark('X'); }
uint8_t *test_framebuffer(EpdiyHighlevelState *hl) { (void)hl;return pixels; }
void test_poweron(void) { assert(!strcmp(order,"A"));mark('P'); }
void test_clear(void) { mark('C'); }
void test_white(EpdiyHighlevelState *hl) { (void)hl;mark('W'); }
void test_image(uint8_t *fb,const uint8_t *image) { (void)fb;drawn=image;mark('I'); }
void test_power_screen(uint8_t *fb,ui_power_screen_t state,const uint8_t *image) { (void)fb;(void)image;assert(state==UI_POWER_SHUTTING_DOWN);mark('O'); }
enum EpdDrawError test_from_white(EpdiyHighlevelState *hl,enum EpdDrawMode mode,int temperature) { (void)hl;assert(mode==MODE_GC16 && temperature==25);mark('D');return fail_display?(enum EpdDrawError)1:EPD_DRAW_SUCCESS; }
enum EpdDrawError test_full(EpdiyHighlevelState *hl) { (void)hl;mark('D');return fail_display?(enum EpdDrawError)1:EPD_DRAW_SUCCESS; }
static void exercise(bool demo,app_sleep_mode_t mode,bool available,bool has_cover,bool failure) {
    chosen=mode;ready=available;fail_display=failure;calls=0;order[0]=0;drawn=NULL;commit_seen=false;
    app_lock_set_image(has_cover?cover:NULL);
    assert(app_lock_has_image()==has_cover);
    EpdiyHighlevelState hl={0};app_ctx_t ctx={.hl=&hl,.fb=pixels};
    if(!setjmp(displayed)) {
        if(demo)sleep_show_notice(&ctx,mode);
        else enter_lock_and_sleep(&hl,NULL,NULL);
        assert(0);
    }
    bool wall=available && (mode==APP_SLEEP_OFF || !has_cover);
    assert(commit_seen==(wall && !failure));
    if(mode==APP_SLEEP_OFF && !wall)assert(!strcmp(order,demo?"AODF":"APCWODF"));
    else assert(!strcmp(order,demo?"AIDF":"APCWIDF"));
    if(wall)assert(drawn==pixels);
    else if(mode!=APP_SLEEP_OFF)assert(drawn==(has_cover?cover:lock_4bpp_bin_start));
}
int main(void) {
    exercise(false,APP_SLEEP_DEEP,true,false,false);
    exercise(false,APP_SLEEP_DEEP,true,true,false);
    exercise(false,APP_SLEEP_DEEP,false,false,false);
    exercise(false,APP_SLEEP_LIGHT,true,false,false);
    exercise(false,APP_SLEEP_OFF,true,true,false);
    exercise(false,APP_SLEEP_OFF,false,false,false);
    exercise(false,APP_SLEEP_OFF,true,false,true);
    exercise(true,APP_SLEEP_OFF,true,false,false);
    exercise(true,APP_SLEEP_OFF,false,false,false);
    exercise(true,APP_SLEEP_OFF,true,false,true);
    puts("wallpaper power: acquire before power/clear, unchanged GC16/25, cover/default precedence, two OFF routes and success-only commit PASS");
}
