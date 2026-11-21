/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf done;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[UI_GESTURE_LONG_DRAG+1];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int wallpaper_steps,wallpaper_invalidations;
static bool wallpaper_guard_exit;
void reader_wallpaper_idle(bool off,bool cover) { (void)off;(void)cover;assert(!samples[step].count);++wallpaper_steps; }
void reader_wallpaper_invalidate(void) { ++wallpaper_invalidations; }
app_sleep_mode_t app_settings_sleep_mode(void) { return APP_SLEEP_DEEP; }
bool app_lock_has_image(void) { return false; }
static int long_keys;
static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second;
static bool full_tick, lock_due, font_due;
static bool lock_hook_test, private_cover, power_down, lock_requires_cancel;
static int lock_hooks;
static bool clock_retry_case, clock_cleanup_complete;
static int clock_busy_returns, clock_cancel_calls, power_events, sleep_calls, sleep_step;
static int first_cancel_step, ready_cancel_step;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32];
static int media_lost, builtin_opens, font_opens, probes, loss_step;
int E0470_WAVEFORM;
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
bool continuous_du_init(void) {return true;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;(void)a;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;fulls++;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
void rails_idle_check(int64_t n) {(void)n;}
bool read_pico_pmu_ready(void) {return lock_due;}
bool read_pico_pmu_take_key_short(void) {
    if(!clock_retry_case)return true;
    if(step!=2)return false;
    ++power_events;return true;
}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a) {
    (void)h;(void)t;(void)a;
    if(clock_retry_case)assert(clock_cleanup_complete);
    if(lock_hook_test)assert(lock_hooks==1 && !private_cover);
    ++sleep_calls;sleep_step=step;lock_due=false;time_offset+=1000000;
}
bool app_lock_powers_down(void) {return power_down;}
bool reader_clock_cancel(void) {
    ++clock_cancel_calls;
    if(clock_busy_returns>0) {
        if(lock_hook_test)assert(lock_hooks==0 && private_cover);
        --clock_busy_returns;first_cancel_step=step;clock_cleanup_complete=false;return false;
    }
    ready_cancel_step=step;clock_cleanup_complete=true;return true;
}
const char* app_settings_font_path(void) {return font_due||saved_sd_font?"/sdcard/fonts/test.ttf":"builtin";}
bool ttf_font_path_is_builtin(const char*p) {(void)p;return !font_due&&!saved_sd_font;}
bool ttf_font_ready(void) {return true;}
bool ttf_font_is_builtin(void) {return !font_due&&!sd_font;}
const char* ttf_font_path(void) {return "other";}
int ttf_font_open(const char*p) {(void)p;font_opens++;sd_font=true;return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
bool ttf_font_on_card(void) {return sd_font;}
int ttf_font_open_fallback(void) {return ttf_font_open_builtin();}
int read_pico_sd_get_info(read_pico_sd_info_t*i) {i->mounted=media_test?mounted_steps[step]:font_due;return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
void read_pico_sd_start_probe(void) {probes++;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:NULL;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) { if(lock_hook_test)assert(lock_hooks==1 && !private_cover); if(wallpaper_guard_exit)assert(wallpaper_invalidations>0);(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const cst836u_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static void lock_hook(app_ctx_t*c) {
    (void)c;assert(clock_cleanup_complete);
    if(lock_requires_cancel)assert(events[UI_GESTURE_CANCEL]==1);
    ++lock_hooks;private_cover=false;
}
static void reset(void) {
    lock_hook_test=private_cover=power_down=lock_requires_cancel=false;lock_hooks=0;
    wallpaper_steps=wallpaper_invalidations=0;wallpaper_guard_exit=false;
    clock_retry_case=clock_cleanup_complete=false;clock_busy_returns=clock_cancel_calls=power_events=sleep_calls=0;
    sleep_step=first_cancel_step=ready_cancel_step=-1;
    media_test=sd_font=saved_sd_font=false;media_lost=builtin_opens=font_opens=probes=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));
    long_keys=0;
    home_on_touch=home_on_tick=home_on_key=start_second=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.first_app=start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static int deferred_short_step, deferred_long_step;
static bool defer_middle(app_ctx_t*c,int k) {(void)c;return k==UI_KEY_2;}
static app_redraw_t deferred_short(app_ctx_t*c,int k) {
    assert(c->released && c->consumed);keys[k]++;deferred_short_step=step;
    return APP_REDRAW_FULL;
}
static app_redraw_t deferred_long(app_ctx_t*c,int k) {
    assert(k==UI_KEY_2 && c->consumed);long_keys++;deferred_long_step=step;return APP_REDRAW_AREA;
}
static void deferred_case(void) {
    reset();first.owns_keys=true;first.defer_key=defer_middle;
    first.on_key=deferred_short;first.on_key_long=deferred_long;
    deferred_short_step=deferred_long_step=-1;time_step=100000;
}
static void deferred_tests(void) {
    // 短按只在有效抬起时全刷；按住期间不得提前触发。/ Short taps full-refresh only on valid release, never while held.
    deferred_case();add(240,1500,1,0);add(240,1500,1,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    deferred_case();add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(keys[1]==1&&deferred_short_step==2&&!long_keys&&fulls==2);
    // 500ms触发长按，继续按住及抬起都不补短按。/ Hold fires at 500ms; continued contact and release never add a short tap.
    deferred_case();for(int i=0;i<5;i++)add(240,1500,1,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    deferred_case();for(int i=0;i<9;i++)add(240,1500,1,0);add(0,0,0,0);run();
    assert(!keys[1]&&long_keys==1&&deferred_long_step==5&&fulls==1&&du_areas==1);
    deferred_case();time_step=166000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&deferred_short_step==3&&!long_keys&&fulls==2);
    deferred_case();time_step=250000;
    add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!keys[1]&&long_keys==1&&deferred_long_step==2&&fulls==1&&du_areas==1);
    // 离键后返回、多点和读取失败均取消，抬起不得补发。/ Drift, multitouch and read errors cancel permanently until release.
    deferred_case();add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    deferred_case();add(240,1500,1,0);add(240,1500,2,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    deferred_case();add(240,1500,2,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    deferred_case();add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(0,0,0,0);run();
    assert(!keys[1]&&!long_keys&&fulls==1);
    // 锁屏、字体恢复、拔卡和页面请求取消待定短按。/ Lock, font restoration, removal and page requests cancel the pending tap.
    deferred_case();time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(!lock_due&&!keys[1]&&!long_keys);
    deferred_case();font_due=true;time_offset=2900000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(font_opens==1&&!keys[1]&&!long_keys);
    deferred_case();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(240,1500,1,0);add(0,0,0,0);run();assert(media_lost==1&&!keys[1]&&!long_keys);
    deferred_case();request_on_tick=true;
    add(240,1500,1,0);add(0,0,0,0);run();assert(enters==2&&exits==1&&!keys[1]&&!long_keys);
    deferred_case();menu_on_tick=true;
    add(240,1500,1,0);add(0,0,0,0);run();assert(menus==1&&!keys[1]&&!long_keys);
    deferred_case();home_on_tick=true;
    add(240,1500,1,0);add(0,0,0,0);run();assert(menus==1&&!keys[1]&&!long_keys);
    deferred_case();full_tick=true;
    add(240,1500,1,0);add(0,0,0,0);run();assert(!keys[1]&&!long_keys&&fulls==3);
    // 未被谓词选中的键仍在按下时派发。/ Unselected keys still dispatch immediately on press.
    deferred_case();first.on_key=key;add(80,1500,1,0);run();assert(keys[0]==1&&!keys[1]&&!long_keys);
}
static bool hold_selected, full_on_hold;
static int drag_x, drag_y;
static app_redraw_t selection_gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    app_redraw_t redraw=gesture(c,e);
    if(e->type==UI_GESTURE_LONG_PRESS){hold_selected=true;if(full_on_hold)redraw=APP_REDRAW_FULL;}
    if(e->type==UI_GESTURE_LONG_DRAG){drag_x=e->x;drag_y=e->y;}
    if(e->type==UI_GESTURE_CANCEL)hold_selected=false;
    return redraw;
}
static bool allow_drag(app_ctx_t*c) {(void)c;return hold_selected;}
static void drag_case(void) {
    reset();hold_selected=full_on_hold=false;drag_x=drag_y=-1;time_step=250000;
    first.on_gesture=selection_gesture;first.allow_long_drag=allow_drag;
}
static void drag_tests(void) {
    // 先派发长按建立选中，再授权移动；纵移与横移均保留有效松手。/ Dispatch hold to select before allowing vertical and horizontal movement through release.
    drag_case();
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);
    add(100,700,1,0);add(260,700,1,0);add(260,700,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_LONG_PRESS]==1&&events[UI_GESTURE_LONG_DRAG]==2);
    assert(drag_x==260&&drag_y==700&&events[UI_GESTURE_LONG_RELEASE]==1);
    assert(!events[UI_GESTURE_CANCEL]&&!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_D]);
    // 不提供授权的页面保留旧取消规则。/ Pages without permission retain normal movement cancellation.
    drag_case();first.allow_long_drag=NULL;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(100,700,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_LONG_PRESS]==1&&!events[UI_GESTURE_LONG_DRAG]);
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_LONG_RELEASE]);
    // 有谓词但没有命中选中时也不能授权拖动。/ A predicate without a selected target must also reject dragging.
    drag_case();first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(100,700,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_LONG_PRESS]==1&&!events[UI_GESTURE_LONG_DRAG]);
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_LONG_RELEASE]);
    // 读错、多点与全刷均取消已经授权的长按。/ Read errors, multitouch and full redraw cancel an already permitted hold.
    for(int kind=0;kind<3;++kind){
        drag_case();
        add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);
        if(kind==0)add(0,0,0,9);
        if(kind==1)add(100,700,2,0);
        if(kind==2)full_on_hold=true;
        add(0,0,0,0);run();
        assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_LONG_RELEASE]);
        assert(!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_D]);
    }
}
static void pending_clock_lock_test(void) {
    // 唯一一次电源事件在校时未清理完时保留；后续无新按键仍重试并只睡一次。
    // Preserve a single power request while clock cleanup is busy; retry without another key and sleep exactly once.
    reset();lock_hook_test=private_cover=true;first.on_lock=lock_hook;clock_retry_case=true;clock_busy_returns=1;lock_due=true;time_step=1000000;
    for(int i=0;i<6;i++)add(0,0,0,0);
    run();
    assert(power_events==1 && clock_cancel_calls==2 && first_cancel_step==2 && ready_cancel_step==3);
    assert(sleep_calls==1 && sleep_step==3 && !lock_due && clock_cleanup_complete);
    assert(lock_hooks==1 && exits==0);
}
static void lock_hook_tests(void) {
    for(int deep=0;deep<2;deep++) {
        reset();lock_hook_test=private_cover=lock_requires_cancel=true;
        power_down=deep;first.on_lock=lock_hook;first.on_gesture=gesture;
        lock_due=true;time_step=1000000;
        add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
        assert(lock_hooks==1 && sleep_calls==1 && exits==deep && enters==1+deep);
    }
    reset();lock_hook_test=private_cover=true;first.on_lock=lock_hook;
    first.blocks_power_lock=first.blocks_idle_sleep=true;lock_due=true;time_step=4000000;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!lock_hooks && !sleep_calls && !exits && private_cover);
}
int main(void) {
    pending_clock_lock_test();
    lock_hook_tests();
    drag_tests();
    deferred_tests();
    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;time_step=250000;
    add(100,400,1,0);add(100,400,1,0);add(101,400,1,0);add(101,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_LONG_PRESS]==1&&events[UI_GESTURE_LONG_RELEASE]==1);
    assert(!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_CANCEL]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[0]);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_LONG_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&tick_consumed[2]);
    assert(!events[UI_GESTURE_LONG_RELEASE]&&!events[UI_GESTURE_TAP]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    // 产品页无来源返回也回书库，不得弹出演示菜单。/ Product returns without history land in the library, never demos.
    home_case();second.no_menu=true;second.owns_keys=true;home_on_tick=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(menus==0&&home_enters==1);
    // 产品页的遗留菜单请求也只能返回书库。/ Legacy menu requests from product pages also return to the library.
    home_case();second.no_menu=true;second.owns_keys=true;menu_on_tick=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(menus==0&&home_enters==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[1]=true;time_step=250000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_LONG_PRESS]==1&&events[UI_GESTURE_CANCEL]==1);
    assert(!events[UI_GESTURE_LONG_RELEASE]&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&font_opens==1);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==2);
    // USB 独占页不能重开字体或因电源短按离页。/ Exclusive USB pages cannot reopen fonts or leave on a short power press.
    reset();first.preloads_wallpaper=true;first.blocks_media_access=true;first.blocks_power_lock=true;first.blocks_idle_sleep=true;
    font_due=lock_due=true;time_step=4000000;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(lock_due && !font_opens && !probes && !media_lost && ticks==3);
    assert(!wallpaper_steps && wallpaper_invalidations>=3);
    reset();media_test=true;memset(mounted_steps,1,sizeof(mounted_steps));first.preloads_wallpaper=true;time_step=600000;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();assert(wallpaper_steps==2);
    reset();media_test=true;memset(mounted_steps,1,sizeof(mounted_steps));first.preloads_wallpaper=true;time_step=600000;full_tick=true;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();assert(!wallpaper_steps);
    reset();media_test=true;memset(mounted_steps,1,sizeof(mounted_steps));first.preloads_wallpaper=true;time_step=600000;add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);run();assert(!wallpaper_steps);
    reset();wallpaper_guard_exit=true;request_on_tick=true;add(0,0,0,0);run();assert(wallpaper_invalidations>0);
    reset();menu_on_tick=true;add(0,0,0,0);run();assert(wallpaper_invalidations>0);
    reset();home_on_tick=true;add(0,0,0,0);run();assert(wallpaper_invalidations>0);
    reset();media_test=true;memset(mounted_steps,1,sizeof(mounted_steps));time_step=600000;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();assert(!wallpaper_steps);
    reset();media_test=true;memset(mounted_steps,1,sizeof(mounted_steps));first.preloads_wallpaper=true;request_on_tick=true;time_step=600000;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();assert(wallpaper_invalidations>0 && !wallpaper_steps);
    puts("app_loop: pending clock-cancel lock retry, opt-in idle wallpaper, input/redraw priority, non-opt-in pages and transition cancellation passed");
    return 0;
}
