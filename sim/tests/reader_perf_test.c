/* SPDX-License-Identifier: Apache-2.0
 * 阅读器续读等待及计时状态回归；包含产品页以检查内部状态。
 * Reader resume-wait and timing regressions; include the product page to inspect its internal state.
 */
#define read_pico_sd_remount test_sd_remount
#define read_pico_sd_get_info test_sd_get_info
#include "../../main/apps/app_reader.c"
#undef read_pico_sd_remount
#undef read_pico_sd_get_info
#include <assert.h>
#include "read_pico_board.h"

extern esp_err_t read_pico_sd_remount(void);
extern esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
extern void sim_sd_set_present(bool present);
static unsigned remount_calls;
static bool fail_mount, probe_pending, defer_mount;

// 仅替换硬件边界，成功路径仍使用模拟器的真实挂载状态与文件列表。
// Replace only the hardware boundary; success uses real simulator mount state and file listing.
esp_err_t test_sd_remount(void) {
    assert(s_text == NULL && s_cover == NULL && !ttf_font_on_card());
    ++remount_calls;
    if (defer_mount) {
        probe_pending = true;
        return ESP_ERR_NOT_FINISHED;
    }
    return fail_mount ? ESP_FAIL : read_pico_sd_remount();
}
esp_err_t test_sd_get_info(read_pico_sd_info_t* info) {
    esp_err_t err = read_pico_sd_get_info(info);
    if (probe_pending) {
        info->mounted = false;
        info->error = ESP_ERR_NOT_FINISHED;
        return ESP_ERR_NOT_FINISHED;
    }
    return err;
}

int main(void) {
    epd_init(&epd_board_read_pico, &E0470_DISPLAY, EPD_LUT_1K);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    app_ctx_t ctx = {0};
    s_books = calloc(1, sizeof(*s_books));
    assert(s_books);
    s_book_n = 1;
    s_view = VIEW_LIBRARY;
    s_resume_pending = true;
    s_page_start[1] = 0;
    const ui_gesture_event_t swipe = {.type = UI_GESTURE_SWIPE_L};
    assert(on_gesture(&ctx, &swipe) == APP_REDRAW_NONE);
    assert(s_lib_page == 0 && s_resume_pending);
    assert(on_key(&ctx, UI_KEY_1) == APP_REDRAW_NONE && !s_menu);
    assert(on_key(&ctx, UI_KEY_3) == APP_REDRAW_NONE);
    s_book_n = 0;
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && !s_resume_pending);
    s_text = "x";
    s_page = s_chapter = 0;
    s_work_started_us = 0;
    assert(turn_page(-1) == APP_REDRAW_NONE);
    assert(s_work_started_us == 0);
    s_text = NULL;
    free(s_books);
    s_books = NULL;
    puts("reader resume wait and no-op timing: PASS");
    // 卡仍插着但挂载丢失：书库应主动恢复，而不是永远只剩内置书。
    // A present card with a lost mount must recover without requiring a reboot.
    FILE* fixture = fopen("/sdcard/books/Recovered.epub", "wb");
    assert(fixture);
    fclose(fixture);
    s_card_missing = true;
    s_card_probing = false;
    s_card_poll_ms = 0;
    s_book_n = 0;
    s_view = VIEW_LIBRARY;
    s_text = strdup("owned page text");
    s_cover = calloc(16, 1);
    assert(s_text && s_cover);
    app_lock_set_image(s_cover);
    (void)read_pico_sd_sync();
    assert(!card_ready());
    ctx.now_ms = 1000;
    (void)on_tick(&ctx);
    ctx.now_ms = 2000;
    (void)on_tick(&ctx);
    assert(card_ready());
    assert(!s_card_missing);
    assert(s_book_n == 1 && strcmp(s_books[0].name, "Recovered.epub") == 0);
    assert(remount_calls == 1);

    // 正在读书、触摸、菜单或挂载期间不能卸载；持续失败必须限次并退避。
    // Never unmount during reading, touch, menus or an active probe; bound and back off failures.
    (void)read_pico_sd_sync();
    s_card_missing = true;
    fail_mount = true;
    ctx.now_ms = 3000;
    s_view = VIEW_READING;
    (void)recover_library_card(&ctx);
    s_view = VIEW_LIBRARY;
    ctx.pressed = true;
    (void)recover_library_card(&ctx);
    ctx.pressed = false;
    cst836u_touch_t held_touch = {.touched = true, .count = 1};
    ctx.touch = &held_touch;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 1);
    ctx.touch = NULL;
    s_menu = true;
    (void)recover_library_card(&ctx);
    s_menu = false;
    probe_pending = true;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 1);
    probe_pending = false;
    ctx.now_ms = 4000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 2);
    ctx.now_ms = 5000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 2);
    ctx.now_ms = 6000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 3);
    ctx.now_ms = 10000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 4);
    ctx.now_ms = 30000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 4);
    sim_sd_set_present(false);
    ctx.now_ms = 31000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 4);
    sim_sd_set_present(true);
    (void)read_pico_sd_sync();
    fail_mount = false;
    ctx.now_ms = 32000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 5 && card_ready() && !s_card_missing);
    assert(s_book_n == 1);

    // 真机重挂载先返回、后台才完成；期间不能重复卸载或提前把卡算作就绪。
    // Hardware remount returns before its task completes; wait without remounting again.
    app_settings_set_font_path("/sdcard/fonts/Saved.ttf");
    assert(ttf_font_open_fallback() == ESP_OK && !ttf_font_on_card());
    (void)read_pico_sd_sync();
    s_card_missing = true;
    s_resume_pending = true;
    defer_mount = true;
    ctx.now_ms = 33000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 6 && s_card_missing && s_card_probing && s_resume_pending);
    ctx.now_ms = 34000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 6 && s_card_missing);
    probe_pending = defer_mount = false;
    assert(read_pico_sd_remount() == ESP_OK);
    ctx.now_ms = 35000;
    (void)recover_library_card(&ctx);
    assert(remount_calls == 6 && !s_card_missing && s_book_n == 1 && s_resume_pending);
    assert(strcmp(ttf_font_path(), "/sdcard/fonts/Saved.ttf") == 0);
    app_settings_set_font_path("/sdcard/fonts/Missing.ttf");
    library_scan();
    assert(ttf_font_ready() && !s_card_missing && s_book_n == 1);
    assert(strcmp(app_settings_font_path(), "/sdcard/fonts/Missing.ttf") == 0);
    app_settings_set_font_path("/flash/fonts/Fallback.ttf");
    assert(ttf_font_open(app_settings_font_path()) == ESP_OK);
    library_scan();
    assert(strcmp(ttf_font_path(), "/flash/fonts/Fallback.ttf") == 0);
    ttf_font_unload();
    free(s_books);
    s_books = NULL;
    puts("reader present-card mount recovery: PASS");
}
