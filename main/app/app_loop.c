/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 事件循环。只做和页面无关的事：读触摸并去抖、三个实体键与菜单把手的路由、切页
 * 时调 on_exit/on_enter、按回调返回值决定怎么刷屏、电源键锁屏、无操作 2 分钟走
 * 同一锁屏、SD 卡上的字体延迟加载。页面自己的状态和刷新节奏都在 main/apps/ 各自的文件里。
 *
 * Event loop for page-agnostic work: touch debounce, the three keys and
 * the menu handle, on_exit/on_enter, present, lock, the same lock after
 * 2 minutes idle, delayed SD font load.
 * Page state and refresh cadence stay in main/apps/; gesture recognition is shared.
 *
 * 冻结：主循环只产出手势、不解释含义；只有 owns_keys 页可接管三键，
 * 且必须提供强刷和 request_menu 入口；把手仍按下即触发。
 * 接管页可选长按键回调，默认保留按下行为；显式延迟的键只在有效抬起短按，同键单指500ms长按后不补短按，中断取消。
 * 原因：词典长按入口不能先触发全刷。
 * Frozen: Produce gestures without interpreting page actions. KEY2 full refresh
 * and KEY3 menu are global unless owns_keys provides its own refresh/menu exits.
 * Owners may opt into a single 500ms same-key hold callback. Press remains immediate by default; explicitly deferred keys fire on valid release unless held, and interruptions cancel both.
 * Reason: dictionary holds must not trigger a full refresh before opening.
 * The menu handle still fires on press.
 * 冻结：返回消费最近切页来源，恢复原页及菜单位置，不跳固定首页。
 * 冻结：USB独占页暂停介质/字体恢复与短按锁屏，仅保留安全退出。/ Frozen: exclusive USB pages pause media/font recovery and short-press locking until safe exit.
 * Frozen: Return consumes the latest page origin, restoring its page and menu position.
 */

#include "app_loop.h"

#include <string.h>

#include "app_registry.h"
#include "../board/reader_board.h"
#include "display.h"
#include "../board/reader_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "../board/reader_media.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_gesture.h"
#if !READER_METALIO
#include "../board/reader_wallpaper.h"
#include "../board/reader_clock.h"
static unsigned s_wallpaper_draws;
#endif

#define TAG "app_loop"

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 3000
#define MEDIA_POLL_INTERVAL_MS 500

static int64_t s_lock_ignore_until_ms;

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
#if !READER_METALIO
    if(redraw!=APP_REDRAW_NONE)++s_wallpaper_draws;
#endif
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (app->present != NULL && app->present(ctx, redraw)) return;
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
            return;
        case APP_REDRAW_AREA: {
            // 回调已经画好了 fb，这里只负责把它那一块推上屏。
            // The callback already painted fb; push that rectangle only.
            EpdRect area = app->area_hint != NULL
                ? app->area_hint(ctx)
                : ui_content_refresh_area();
            result = update_display_area_with(
                ctx->hl, &READER_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area
            );
            break;
        }
        case APP_REDRAW_FULL:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = APP_PAGE_FORCE_FULL
                ? update_display_full(ctx->hl)
                : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
        case APP_REDRAW_PAGE:
        default:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
    }
    guard_draw_result(ctx->hl, result);
}

// 中断边界先清页面装饰再清识别器。/ Clear decoration before resetting recognition.
static void cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        const app_desc_t* requested = ctx->request_app;
        bool menu_requested = ctx->request_menu;
        bool return_requested = ctx->request_return;
        app_redraw_t redraw = app->on_gesture(ctx, &event);
        ctx->request_app = requested;
        ctx->request_menu = menu_requested;
        ctx->request_return = return_requested;
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
}

static void present_page(app_ctx_t* ctx, const app_desc_t* app,
                         ui_gesture_t* gesture, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)
        cancel_gesture(ctx, app, gesture);
    app_present(ctx, app, redraw);
}

typedef struct {
    unsigned updates;
    int64_t last_ms;
    EpdRect area;
} menu_feedback_t;

// 菜单反馈只推叶内单行。/ Menu feedback pushes one local row only.
static void menu_feedback(app_ctx_t* ctx, const app_desc_t* current,
                          int leaf, int hit, bool pressed, menu_feedback_t* feedback) {
    EpdRect area;
    int row = hit - leaf * UI_MENU_ITEMS_PER_PAGE;
    if (!ui_menu_row_rect(leaf, row, &area)) return;
    ui_draw_menu_row_pressed(ctx->fb, current, leaf, row, pressed);
    if (feedback->updates == 0) feedback->area = area;
    else {
        int bottom = feedback->area.y + feedback->area.height;
        int next_bottom = area.y + area.height;
        if (area.y < feedback->area.y) feedback->area.y = area.y;
        feedback->area.height = (bottom > next_bottom ? bottom : next_bottom) - feedback->area.y;
    }
    feedback->updates++;
    feedback->last_ms = ctx->now_ms;
    guard_draw_result(ctx->hl, update_display_area_with(
        ctx->hl, &READER_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area));
    if (feedback->updates >= UI_SETTLE_DU_MAX) {
        guard_draw_result(ctx->hl, update_display_area_with(
            ctx->hl, &READER_WAVEFORM, MODE_GL16, feedback->area));
        feedback->updates = 0;
    }
}

// 切页：先让上一页收尾，再给新页一次 on_enter，最后整页画出来。
// Leave the old page, enter the new one, then present a full page.
static void switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (next == NULL || next == *current) return;
#if !READER_METALIO
    reader_wallpaper_invalidate();
#endif
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
    *current = next;
    if (next->on_enter != NULL) next->on_enter(ctx);
    app_present(ctx, next, next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    ESP_LOGI(TAG, "page -> %s", next->title);
}

// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf, menu_feedback_t* feedback) {
#if !READER_METALIO
    reader_wallpaper_invalidate();
#endif
    feedback->updates = 0;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
}

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    reader_touch_t* latest, const reader_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < READER_TOUCH_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < READER_TOUCH_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

static bool try_load_saved_sd_font(bool allow_probe) {
    const char* path = app_settings_font_path();
    if (ttf_font_path_is_builtin(path)) return false;
    // 存的是机身字体：开机时已经处理过，不靠卡，这里不重试。/ A saved built-in-storage font was handled at boot and needs no card; no retry here.
    if (strncmp(path, "/sdcard/", 8) != 0) return false;
    if (ttf_font_ready() && !ttf_font_is_builtin()
        && strcmp(ttf_font_path(), path) == 0) {
        return false;
    }

    reader_media_info_t info;
    esp_err_t err = reader_media_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    if (!info.mounted) {
        if (allow_probe && err == ESP_ERR_INVALID_STATE) reader_media_start_probe();
        return false;
    }
    return ttf_font_open(path) == ESP_OK && !ttf_font_is_builtin();
}

// 先停文件使用者，再换字体；此处只观察挂载，不卸载也不重新挂载。
// Stop file consumers before font fallback; observe mounting without unmounting or remounting.
static bool poll_media(app_ctx_t* ctx, const app_desc_t* current,
                       bool* mounted, bool* invalidated) {
    if(current->blocks_media_access) return false;
    reader_media_info_t info = {0};
    esp_err_t err = reader_media_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.mounted;
    bool lost = *mounted && !ready;
    *mounted = ready;
    if (ready) *invalidated = false;
    if (!lost) return false;
    *invalidated = true;
#if !READER_METALIO
    reader_wallpaper_invalidate();
#endif
    if (current->on_media_lost) current->on_media_lost(ctx);
    if (ttf_font_ready() && ttf_font_on_card()) ttf_font_open_fallback();
    return true;
}

// 电源键和空闲超时共用：收掉手势，走锁屏路径，浅睡回来后重画。
// Shared by the power key and the idle timeout: cancel input, lock, redraw after light sleep.
static bool s_clock_lock_pending;
static void lock_and_resume(
    app_ctx_t* ctx, const app_desc_t* current, ui_gesture_t* gesture,
    int* held_key, int* menu_pressed, menu_feedback_t* feedback,
    bool menu_open, int menu_leaf, bool* media_mounted, bool* media_invalidated,
    int64_t* last_media_poll_ms
) {
#if !READER_METALIO
    // 先释放短时校时的网络；清理未完则下次轮询继续，不丢掉锁屏请求。/ Release time-sync networking first; retry unfinished cleanup without losing the lock request.
    if(!reader_clock_cancel()){s_clock_lock_pending=true;return;}
#endif
    s_clock_lock_pending=false;
    *held_key = -1;
    cancel_gesture(ctx, current, gesture);
    // 浅睡与下电都先清理页面隐私/临时状态，再读取封面或壁纸。
    // Clear page privacy/transient state before acquiring any cover or wallpaper, for light sleep and power-down alike.
    if (current->on_lock != NULL) current->on_lock(ctx);
    if (*menu_pressed >= 0) {
        menu_feedback(ctx, current, menu_leaf, *menu_pressed, false, feedback);
    }
    *menu_pressed = UI_MENU_HIT_NONE;
    // 要断主机电时先让页面收尾（停网络、关传感器、存进度）；只有下电失败才会回到这里重新进页。
    // Before the host loses power let the page wind down (network, sensors, progress); re-enter only if power-down failed.
    const bool powers_down = app_lock_powers_down();
    if (powers_down && current->on_exit != NULL) current->on_exit(ctx);
    enter_lock_and_sleep(ctx->hl, &s_lock_ignore_until_ms, ctx->acc);
    if (powers_down && current->on_enter != NULL) current->on_enter(ctx);
    ctx->now_ms = esp_timer_get_time() / 1000;
    poll_media(ctx, current, media_mounted, media_invalidated);
    *last_media_poll_ms = ctx->now_ms;
    ctx->consumed = true;
    ctx->pressed = false;
    ctx->released = false;
    if (menu_open) present_menu(ctx, current, menu_leaf, feedback);
    else app_present(ctx, current, APP_REDRAW_PAGE);
}

void app_loop_run(const app_loop_config_t* config) {
    const app_desc_t* current = config->first_app != NULL
        ? config->first_app
        : app_home_page();

    reader_touch_info_t touch_info = { 0 };
    reader_touch_get_info(config->tp, &touch_info);
    reader_touch_t latest = { 0 };

    app_ctx_t ctx = {
        .hl = config->hl,
        .fb = config->fb,
        .acc = config->acc,
        .tp = config->tp,
        .sensor_ready = config->sensor_ready,
        .continuous_ready = reader_board_continuous_init(),
        .touch = &latest,
        .touch_info = &touch_info,
    };

    bool was_touched = false;
    bool menu_open = false;
    ui_gesture_t gesture = {0};
    int menu_pressed = UI_MENU_HIT_NONE;
    menu_feedback_t feedback = {0};
    int menu_leaf = 0;
    const app_desc_t* return_app = NULL;
    int return_leaf = 0, return_menu_leaf = 0;
    bool return_to_menu = false;
    int held_key = -1;
    bool held_deferred = false;
    int64_t held_since_ms = 0;
    int64_t last_lock_poll_ms = 0;
    int64_t last_input_ms = esp_timer_get_time() / 1000;
    int64_t last_font_retry_ms = 0;
    int64_t last_media_poll_ms = 0;
    bool media_mounted = ttf_font_ready() && ttf_font_on_card();
    bool media_invalidated = false;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    if (current->on_enter != NULL) current->on_enter(&ctx);
    poll_media(&ctx, current, &media_mounted, &media_invalidated);
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    ESP_LOGI(TAG, "UI ready on %s", current->title);

    while (true) {
#if !READER_METALIO
        unsigned wallpaper_before=s_wallpaper_draws;
        if(current->blocks_media_access)reader_wallpaper_invalidate();
#endif
        reader_touch_t touch = { 0 };
        esp_err_t err = reader_touch_read(config->tp, &touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
            memset(&touch, 0, sizeof(touch));
        }

        const bool pressed = touch.touched && !was_touched;
        const bool released = !touch.touched && was_touched;
        debounce_touch(&latest, &touch, released);
        const int64_t touch_ms = esp_timer_get_time() / 1000;
        ctx.now_ms = touch_ms;
        // 触摸、独占 PMU、以及声明不自动休眠的页都算活动。页面自己的轮询刷新不重置。
        // Touch, PMU-owning pages, and pages that block idle sleep count as activity. A page's own poll does not reset it.
        if (current->holds_pmu || current->blocks_idle_sleep
            || (err == ESP_OK && (touch.touched || released))) {
            last_input_ms = ctx.now_ms;
        }
        ctx.pressed = pressed;
        ctx.released = released;
        ctx.consumed = false;
        ctx.request_app = NULL;
        ctx.request_menu = false;
        ctx.request_return = false;
        bool selected_from_menu = false;

        if (ctx.now_ms - last_media_poll_ms >= MEDIA_POLL_INTERVAL_MS) {
            last_media_poll_ms = ctx.now_ms;
            if (poll_media(&ctx, current, &media_mounted, &media_invalidated)) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
                ctx.pressed = ctx.released = false;
                was_touched = touch.touched;
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_FULL);
                // 本轮旧输入不再触发动作，下一轮恢复正常事件处理。
                // Discard this tick's stale input, then resume normal dispatch next tick.
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }

        if (err != ESP_OK) {
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            ctx.consumed = true;
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
        }
        if (pressed && err == ESP_OK) {
            held_key = -1;
            const int key = ui_key_hit_test(touch.x, touch.y);
            // 产品页不画把手，菜单没开时右下角归页面自己。
            // Product pages draw no handle; with the menu closed that corner belongs to the page.
            const bool handle_hit = (menu_open || !current->no_menu)
                && ui_menu_handle_hit_test(touch.x, touch.y);
            ctx.consumed = true;
            if (key >= 0 || handle_hit) {
                cancel_gesture(&ctx, current, &gesture);
                if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
            }
            if (key >= 0 && current->owns_keys && !menu_open) {
                held_deferred = current->defer_key && current->defer_key(&ctx, key);
                if (!held_deferred) {
                    app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                    present_page(&ctx, current, &gesture, redraw);
                }
                if ((held_deferred || current->on_key_long) && touch.count == 1) {
                    held_key = key;
                    held_since_ms = ctx.now_ms;
                }
            } else if (key == UI_KEY_2) {
                if (menu_open) {
                    feedback.updates = 0;
                    ui_draw_menu_page(ctx.fb, current, menu_leaf);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                } else if (current->present != NULL && current->present(&ctx, APP_REDRAW_FULL)) {
                } else if (current->render != NULL) {
                    current->render(&ctx, ctx.fb);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                }
            } else if (key == UI_KEY_3 || handle_hit) {
                menu_open = !menu_open;
                if (menu_open) {
                    menu_leaf = ui_menu_leaf_for_app(current);
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else app_present(&ctx, current, APP_REDRAW_PAGE);
            } else if (key >= 0 && menu_open) {
                int next = menu_leaf + (key == UI_KEY_1 ? -1 : 1);
                if (next >= 0 && next < ui_menu_leaf_count()) {
                    menu_leaf = next;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                }
            } else if (key >= 0) {
                app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                if (redraw == APP_REDRAW_NONE)
                    ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
                present_page(&ctx, current, &gesture, redraw);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else if (hit >= 0 && touch.count == 1) {
                    menu_pressed = hit;
                    menu_feedback(&ctx, current, menu_leaf, hit, true, &feedback);
                }
            } else if (current->on_gesture) {
                ctx.consumed = false;
            } else {
                // 旧页按下先收到 consumed=true，仅 NONE 为同轮 tick 放行。
                // Legacy press sees consumed=true; only NONE clears it for this tick.
                app_redraw_t redraw = current->on_touch
                    ? current->on_touch(&ctx, &latest) : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        if (err == ESP_OK) was_touched = touch.touched;
        if (menu_open && menu_pressed >= 0) {
            int hit = ui_menu_hit_test(latest.x, latest.y, menu_leaf);
            if (touch.count > 1 || hit != menu_pressed || released) {
                int chosen = menu_pressed;
                menu_feedback(&ctx, current, menu_leaf, chosen, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
                if (released && err == ESP_OK && hit == chosen) {
                    menu_open = false;
                    ctx.request_app = app_at(chosen);
                    selected_from_menu = true;
                    if (ctx.request_app == current) app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }
        if (!menu_open && current->on_gesture && err == ESP_OK) {
            ui_gesture_event_t event;
            if (ui_gesture_feed(&gesture, &ctx, &event)) {
                app_redraw_t redraw = current->on_gesture(&ctx, &event);
                if (event.type == UI_GESTURE_LONG_PRESS && current->allow_long_drag)
                    ui_gesture_allow_long_drag(&gesture, current->allow_long_drag(&ctx));
                present_page(&ctx, current, &gesture, redraw);
                if (redraw != APP_REDRAW_NONE) ctx.consumed = true;
            }
        }

#if READER_METALIO
        const int side_key = reader_board_take_side_key();
        if (side_key >= 0 && !menu_open && current->on_key) {
            cancel_gesture(&ctx, current, &gesture);
            held_key = -1;
            ctx.consumed = true;
            last_input_ms = esp_timer_get_time() / 1000;
            present_page(&ctx, current, &gesture, current->on_key(&ctx, side_key));
        }
#endif
        ctx.now_ms = esp_timer_get_time() / 1000;
        rails_idle_check(ctx.now_ms);

        // 电源键短按或空闲超时 = 锁屏。自检页要连着占用 PMU，这时不抢它的事件队列。
        // Short power-key press or idle timeout locks. A page that holds the PMU keeps its event queue.
        if (reader_board_power_ready() && !current->holds_pmu
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS) {
            last_lock_poll_ms = ctx.now_ms;
            if (reader_board_take_power() || s_clock_lock_pending) {
                if (current->blocks_power_lock || ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot KEY_SHORT");
                } else {
                    lock_and_resume(
                        &ctx, current, &gesture, &held_key, &menu_pressed, &feedback,
                        menu_open, menu_leaf, &media_mounted, &media_invalidated,
                        &last_media_poll_ms
                    );
                    last_input_ms = ctx.now_ms;
                }
            } else if (!current->blocks_power_lock && ctx.now_ms >= s_lock_ignore_until_ms
                       && ctx.now_ms - last_input_ms >= APP_AUTO_SLEEP_MS) {
                ESP_LOGI(TAG, "idle lock");
                lock_and_resume(
                    &ctx, current, &gesture, &held_key, &menu_pressed, &feedback,
                    menu_open, menu_leaf, &media_mounted, &media_invalidated,
                    &last_media_poll_ms
                );
                last_input_ms = ctx.now_ms;
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试。
        // The saved font lives on the card; retry until the mount is ready.
        // The saved font lives on the card; retry until the mount is ready.
        if (!current->blocks_media_access && ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            if (try_load_saved_sd_font(!media_invalidated)) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                menu_pressed = UI_MENU_HIT_NONE;
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
        }

        if (held_key >= 0) {
            if (menu_open || ctx.request_app || ctx.request_menu || ctx.request_return ||
                ctx.now_ms < held_since_ms) {
                held_key = -1;
            } else if (released && held_deferred && ui_key_hit_test(latest.x, latest.y) == held_key) {
                int key = held_key;
                held_key = -1;
                ctx.consumed = true;
                // 500ms 恰好落在本次松手采样时仍算长按。/ A release sampled at 500 ms still qualifies as a hold.
                app_redraw_t redraw = APP_REDRAW_NONE;
                if (current->on_key_long && touch_ms - held_since_ms >= UI_LONG_PRESS_MS) {
                    redraw = current->on_key_long(&ctx, key);
                } else if (current->on_key) {
                    redraw = current->on_key(&ctx, key);
                }
                present_page(&ctx, current, &gesture, redraw);
            } else if (!touch.touched || touch.count != 1 || ui_key_hit_test(touch.x, touch.y) != held_key) {
                held_key = -1;
            } else if (!pressed && current->on_key_long && ctx.now_ms - held_since_ms >= UI_LONG_PRESS_MS) {
                int key = held_key;
                held_key = -1;
                ctx.consumed = true;
                present_page(&ctx, current, &gesture, current->on_key_long(&ctx, key));
            }
        }
        if (!menu_open && !ctx.request_app && !ctx.request_menu && !ctx.request_return && current->on_tick != NULL) {
            app_redraw_t redraw = current->on_tick(&ctx);
            if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) held_key = -1;
            present_page(&ctx, current, &gesture, redraw);
        }
        if (menu_open && feedback.updates > 0
            && ctx.now_ms - feedback.last_ms >= UI_SETTLE_IDLE_MS) {
            guard_draw_result(ctx.hl, update_display_area_with(
                ctx.hl, &READER_WAVEFORM, MODE_GL16, feedback.area));
            feedback.updates = 0;
        }
        if (!menu_open) feedback.updates = 0;
        // 返回来源优先；普通请求仍按切页优先处理。
        // Return requests win; ordinary requests still prioritize page switches.
        if (ctx.request_return || ctx.request_app || ctx.request_menu) {
#if !READER_METALIO
            reader_wallpaper_invalidate();
#endif
            held_key = -1;
            const app_desc_t* next = ctx.request_app;
            const bool go_back = ctx.request_return;
            cancel_gesture(&ctx, current, &gesture);
            menu_pressed = UI_MENU_HIT_NONE;
            ctx.request_app = NULL;
            ctx.request_menu = false;
            ctx.request_return = false;
            ctx.consumed = true;
            if (current->no_menu && (go_back || !next)) {
                // 产品页永不进入演示菜单；不依赖可能丢失的单层历史。/ Product exits never open demos or depend on a single history slot.
                menu_open = false;
                return_app = NULL;
                switch_to(&ctx, &current, app_home_page());
            } else if (go_back) {
                menu_open = return_app ? return_to_menu : true;
                menu_leaf = return_app ? return_menu_leaf : ui_menu_leaf_for_app(current);
                if (return_app) {
                    if (current->on_exit) current->on_exit(&ctx);
                    current = return_app;
#if !READER_METALIO
                    if(current->blocks_media_access)reader_wallpaper_invalidate();
#endif
                    return_app = NULL;
                    if (current->on_enter) current->on_enter(&ctx);
                    // 初始化后恢复通用页码，避免 on_enter 的默认页码覆盖来源。
                    // Restore the shared leaf after initialization overrides its default.
                    ctx.leaf = return_leaf;
                }
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_PAGE);
            } else if (next) {
                if (next != current) {
                    return_app = current;
                    return_leaf = ctx.leaf;
                    return_to_menu = selected_from_menu;
                    return_menu_leaf = menu_leaf;
                }
                menu_open = false;
                switch_to(&ctx, &current, next);

            } else {
                menu_open = true;
                menu_leaf = ui_menu_leaf_for_app(current);
                present_menu(&ctx, current, menu_leaf, &feedback);
            }
        }
#if !READER_METALIO
        // 输入与页面工作优先；本轮画过屏就不抢读卡时间。/ Input and page work win; a tick that painted never spends time preloading.
        if(current->preloads_wallpaper && media_mounted && !media_invalidated && !current->blocks_media_access && !menu_open && !touch.touched && !ctx.consumed &&
           wallpaper_before==s_wallpaper_draws && ctx.now_ms-last_input_ms>=500)
            reader_wallpaper_idle(app_settings_sleep_mode()==APP_SLEEP_OFF,app_lock_has_image());
#endif
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}
