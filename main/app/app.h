/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * demo 页的统一接口。每个页面是一个 app_desc_t，放在 main/apps/ 下自己的文件里，
 * 绘制、命中、轮询和页内状态都封在那一个文件中；app_loop.c 只按这套回调调度，
 * 加一个页面不必再改主循环。
 *
 * Shared page contract. Each page is an app_desc_t in its own main/apps/ file.
 * Draw, hit-test, poll and page state stay there. app_loop.c only dispatches
 * these callbacks; adding a page does not touch the loop.
 *
 * 冻结：手势回调替代旧触摸回调；产品页接管三键并提供强刷、中键返回。
 * Frozen: gesture callbacks replace legacy touch callbacks; product pages own keys, full refresh and middle-key back.
 * 冻结：用户要求删除系统 demo；产品页 no_menu 不得请求或显示演示菜单。
 * 产品页的无目标返回回书库；显式 request_app 用于原生功能之间切换。
 * Frozen: demos are removed at user request; no_menu product pages never open demo menus.
 * Untargeted product returns land in the library; explicit request_app switches native features.
 * 非产品测试页仍保留通用菜单来源恢复，供独立驱动测试使用。
 * Non-product test pages retain generic origin restoration for standalone driver tests.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../board/reader_input.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "app_config.h"
#include "sc7a20h_lab.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 回调返回值告诉主循环该怎么把 framebuffer 推上屏。
/// / What the loop should do with the framebuffer.
typedef enum {
    /// 什么都没变，不刷屏。/ Nothing changed.
    APP_REDRAW_NONE = 0,
    /// 只有 area_hint() 那块变了，使用 APP_DYNAMIC_REFRESH_MODE。回调必须自己先画好 fb。
    /// / Push area_hint() with APP_DYNAMIC_REFRESH_MODE. Caller paints fb first.
    APP_REDRAW_AREA,
    /// 由主循环调 render() 重画整页，使用 APP_PAGE_REFRESH_MODE。
    /// / Loop calls render() and presents with APP_PAGE_REFRESH_MODE.
    APP_REDRAW_PAGE,
    /// 同上；APP_PAGE_FORCE_FULL 置 1 时升为整屏 GC16，否则仍走 APP_PAGE_REFRESH_MODE。
    /// / Same; full-screen GC16 when APP_PAGE_FORCE_FULL is 1.
    APP_REDRAW_FULL,
    /// 回调自己已经刷完屏了（自检、刷新测试这类要分步显示进度的页）。
    /// / Page already presented (self-test, refresh bench, …).
    APP_REDRAW_DONE,
} app_redraw_t;

typedef struct app_desc_s app_desc_t;
typedef struct ui_gesture_event_s ui_gesture_event_t;

typedef struct {
    EpdiyHighlevelState* hl;
    uint8_t* fb;
    sc7a20h_handle_t acc;
    reader_touch_handle_t tp;
    bool sensor_ready;
    bool continuous_ready;
    int64_t now_ms;
    /// 去抖后的触摸快照：空槽已清零，抬手后保留最后一次坐标。
    /// / Debounced touch: empty slots cleared; last coords kept after lift.
    const reader_touch_t* touch;
    const reader_touch_info_t* touch_info;
    /// 本轮是不是按下沿 / 抬起沿，触摸页的跟随节流要用。
    /// / Press/release edge this tick. Touch tracking uses this to throttle.
    bool pressed;
    bool released;
    /// 分页页（字体列表）当前停在第几页，由 KEY1 和页内按钮共同改。
    /// / Leaf page index (font list). KEY1 and in-page buttons both change it.
    int leaf;
    /// 本轮已经被别的分支处理掉了，页内的跟随逻辑要跳过这一轮。
    /// / Another branch already handled this tick; skip in-page tracking.
    bool consumed;
    /// 页面想跳到别的页时填这里，主循环会在本轮末尾切过去。
    /// / Request a page switch; the loop applies it at the end of the tick.
    const struct app_desc_s* request_app;
    /// 本轮末打开菜单；切页请求优先。/ Open the menu at tick end; page requests take priority.
    bool request_menu;
    /// 返回最近切页来源；菜单来源直接恢复菜单，无历史打开当前所属菜单；优先于其它请求。
    /// Return to the latest page origin and its menu; no history opens the current menu leaf. Overrides other requests.
    bool request_return;
} app_ctx_t;

struct app_desc_s {
    /// 菜单里显示的标题与副标题。/ Menu title and subtitle.
    const char* title;
    const char* detail;
    /// true = 这一页会长时间独占 PMU，主循环的锁屏按键轮询要让路（设备功能自检）。
    /// / true = page owns the PMU; lock-key poll yields (device self-test).
    bool holds_pmu;
    /// true = 进页走 APP_REDRAW_FULL（均衡配置下整屏 GC16）。内容和上一页差别大的页面用，避免差分刷留边。
    /// / true = enter with APP_REDRAW_FULL so a differential update does not leave a seam.
    bool enter_full;
    /// 菜单关闭时接管三键，页面必须提供强刷和菜单入口。
    /// Own all three keys outside the menu; provide full refresh and menu exits.
    bool owns_keys;
    /// true = 停留在本页时不计空闲自动休眠。电源键锁屏仍走全局路径。
    /// true = idle time does not auto-sleep while this page is current. The power key still locks.
    bool blocks_idle_sleep;
    /// 独占读卡页暂停主循环的介质检测和 SD 字体恢复。/ Exclusive card pages suspend media polling and saved SD-font restoration.
    bool blocks_media_access;
    /// 仅明确安全的页空闲预读小纸壁纸；默认关闭。/ Opt in to Pico wallpaper preloading at idle on explicitly safe pages; off by default.
    bool preloads_wallpaper;
    /// 传输时消费但忽略短按电源，避免断电和积压按键。/ Consume but ignore short power presses during transfers.
    bool blocks_power_lock;
    /// true = 产品页：屏幕右下角不是演示菜单把手，点那里只是普通触摸。须与 owns_keys 同用。
    /// true = product page: the bottom-right corner is not the demo menu handle; a tap there is an ordinary touch. Use with owns_keys.
    bool no_menu;
    /// 进页/离页。on_enter 里做上电、唤醒传感器、拉一次数据这类副作用。
    /// / Enter/exit. on_enter powers up, wakes sensors, takes a first sample.
    /// 锁屏走软睡/关机前也调 on_exit；下电失败回来再调 on_enter。
    /// on_exit also runs before a soft-sleep/off lock; on_enter runs again if power-down fails.
    void (*on_enter)(app_ctx_t* ctx);
    void (*on_exit)(app_ctx_t* ctx);
    /// 实际锁屏前清理临时状态：手势取消后、封面绘制与 on_exit 前调用；浅睡也调用。
    /// Clear transient state on an actual lock, after gesture cancellation and before cover drawing/on_exit, including light sleep.
    /// 校时清理未完成或页面禁止锁屏时不调用；不得直接睡眠或绘制锁屏。
    /// Not called while clock cleanup is pending or locking is blocked; do not sleep or draw the lock screen here.
    void (*on_lock)(app_ctx_t* ctx);
    /// TF 挂载失效：停止并等待后台任务、关闭文件；主循环随后回退字体并重绘，菜单打开时也通知。
    /// On TF mount loss, join background work and close files before font fallback/redraw; also called behind menus.
    void (*on_media_lost)(app_ctx_t* ctx);
    /// 纯绘制，不做 I2C 写、蜂鸣这类副作用，整屏强刷才能安全复用它。
    /// / Paint only. No I2C writes or buzzer. KEY2 full redraw reuses this.
    void (*render)(app_ctx_t* ctx, uint8_t* fb);
    /// 自定义推屏。返回 true 表示页面已经刷过。
    /// / Custom present. Return true if the page updated the display.
    bool (*present)(app_ctx_t* ctx, app_redraw_t redraw);
    /// 设置后独占触摸派发，替代 on_touch。/ Exclusive gesture dispatch replaces on_touch.
    app_redraw_t (*on_gesture)(app_ctx_t* ctx, const ui_gesture_event_t* event);
    /// 可选纯判断：长按回调确认选中后，仅本次保持允许拖动；缺省越界取消。
    /// Optional pure predicate after LONG_PRESS: allow dragging this selected hold; default cancels outside slop.
    bool (*allow_long_drag)(app_ctx_t* ctx);
    app_redraw_t (*on_touch)(app_ctx_t* ctx, const reader_touch_t* touch);
    /// 默认只收 UI_KEY_1；owns_keys 页面在菜单外接收三键。
    /// / Only KEY1 normally; owns_keys pages receive all three keys outside the menu.
    app_redraw_t (*on_key)(app_ctx_t* ctx, int key);
    /// 可选纯判断：接管键短按延至有效抬起，长按触发后不补短按；缺省按下即触发。
    /// Optional pure predicate: defer an owned key until valid release; a long hold suppresses it. Default fires on press.
    bool (*defer_key)(app_ctx_t* ctx, int key);
    /// 可选：接管页同键单指按住500ms触发一次；默认先按下，defer_key 可延迟短按，滑出/中断取消。
    /// Optional: fire once after a 500ms single-finger same-key hold; press fires first unless defer_key delays it. Leaving/interruption cancels.
    app_redraw_t (*on_key_long)(app_ctx_t* ctx, int key);
    /// 每轮都调，页面自己判断到不到刷新间隔。
    /// / Called every tick; the page decides if its interval has elapsed.
    app_redraw_t (*on_tick)(app_ctx_t* ctx);
    /// APP_REDRAW_AREA 要刷的区域。/ Area for APP_REDRAW_AREA.
    EpdRect (*area_hint)(app_ctx_t* ctx);
    /// 同一份实现挂多个 app 时用来区分（加速度计的读数页与诊断页共享绘制代码）。
    /// / Distinguishes views that share one implementation (IMU live vs lab).
    void* user;
};

// 锁屏：浅睡按键或拿起回原页；软睡 EN=0，再短按开机；关机 EN=0，长按开机。
// Lock: light-sleep key or pickup returns to the page; soft sleep drops EN
// and a short press powers on; power-off drops EN and needs a long press.
// 无操作满这段时间走同一条锁屏路径，档位仍由睡眠设置决定。
// Idle this long, then the same lock path; the sleep setting still picks the mode.
#define APP_AUTO_SLEEP_MS (2 * 60 * 1000)
#define APP_LOCK_POLL_MS 200
#define APP_LOCK_IGNORE_BOOT_MS 2000
#define APP_LOCK_IGNORE_SELFTEST_MS 2000

/// 按 redraw 把 framebuffer 推上屏，顺带做 pclk 欠载兜底。APP_REDRAW_DONE 直接返回。
/// / Present fb for redraw, with a pclk-underrun fallback. APP_REDRAW_DONE returns.
void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw);
/// 之后 ms 毫秒内忽略电源键短按，避免开机或自检的残留事件立刻把屏锁掉。
/// / Ignore short power-key presses for ms so boot/self-test leftovers do not lock.
void app_lock_ignore_for(int64_t ms);

#ifdef __cplusplus
}
#endif
