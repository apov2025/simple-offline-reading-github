/* SPDX-License-Identifier: Apache-2.0
 * 实际阅读页的 Metalio 按键与菜单回归。/ Metalio controls and menu regression against the real reader page.
 */
#define READER_METALIO 1
#include "../../main/apps/app_reader.c"
#include <assert.h>
#include "read_pico_board.h"
#include "read_pico_sd.h"
static bool force_missing;
static unsigned remounts;
esp_err_t reader_media_get_info(reader_media_info_t* out) {
    if (force_missing) {
        *out=(reader_media_info_t){.present=true, .error=ESP_ERR_INVALID_STATE};
        return ESP_ERR_INVALID_STATE;
    }
    read_pico_sd_info_t info;
    esp_err_t err = read_pico_sd_get_info(&info);
    *out = (reader_media_info_t){.present=info.present, .mounted=info.mounted, .error=info.error,
        .capacity_bytes=info.capacity_bytes, .free_bytes=info.free_bytes};
    return err;
}
esp_err_t reader_media_remount(void) {
    remounts++;
    return force_missing ? ESP_FAIL : read_pico_sd_remount();
}
static unsigned light_updates;
void reader_board_set_light(uint8_t level, uint8_t warmth) { (void)level; (void)warmth; ++light_updates; }
int main(void) {
    epd_init(&epd_board_read_pico, &E0470_DISPLAY, EPD_LUT_1K);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    app_settings_init();
    app_ctx_t ctx = {0};
    s_view = VIEW_READING;
    assert(on_key_long(&ctx, UI_KEY_3) == APP_REDRAW_NONE && reader_surface() == SURFACE_BODY);
    s_view = VIEW_LIBRARY;
    s_resume_pending = true;
    s_menu = true;
    assert(on_key(&ctx, UI_KEY_1) == APP_REDRAW_PAGE);
    assert(!s_resume_pending && !s_menu && s_view == VIEW_LIBRARY);
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && s_menu);
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && !s_menu);
    assert(on_key(&ctx, UI_KEY_3) == APP_REDRAW_PAGE && s_force_full);
    assert(ttf_font_count() == 2);
    menu_geom_t g;
    menu_geometry(&g);
    assert(g.panel.y >= 0 && g.panel.y + g.panel.height == 1216);
    assert(app_settings_light_level() == 0);
    assert(app_settings_light_warmth() == 1);
    s_menu = true;
    assert(menu_tap(g.light[2].x + 10, g.light[2].y + 10) == APP_REDRAW_PAGE);
    assert(app_settings_light_level() == 2 && light_updates == 1);
    assert(menu_tap(g.warmth[2].x + 10, g.warmth[2].y + 10) == APP_REDRAW_PAGE);
    assert(app_settings_light_warmth() == 2 && light_updates == 2);
    const char* preview=getenv("READER_MENU_PREVIEW");
    if(preview) {
        // 直接绘制真阅读器菜单供目视检查。/ Render the actual reader menu for visual inspection.
        assert(ttf_font_init()==ESP_OK);
        EpdiyHighlevelState hl=epd_hl_init(&READER_WAVEFORM);
        ctx.hl=&hl;ctx.fb=epd_hl_get_framebuffer(&hl);
        render(&ctx,ctx.fb);
        char path[512];
        assert(reader_screenshot_write(preview,"metalio-menu",ctx.fb,1216,684,path,sizeof(path)));
    }
    app_settings_set_light(255, 255);
    assert(app_settings_light_level() == 0 && app_settings_light_warmth() == 1);
    // 无检测脚：耗尽自动重试后，HOME 主动重新检查插入的卡。/ Without card detect, HOME rearms exhausted retries.
    force_missing=true;s_view=VIEW_LIBRARY;s_menu=false;s_card_missing=true;
    s_card_retry_count=3;s_card_retry_after_ms=100000;ctx.now_ms=20000;
    assert(recover_library_card(&ctx)==APP_REDRAW_NONE && remounts==0);
    assert(on_key(&ctx, UI_KEY_1)==APP_REDRAW_PAGE && s_card_retry_count==0);
    ctx.now_ms+=1000;
    assert(recover_library_card(&ctx)==APP_REDRAW_PAGE && remounts==1);
    force_missing=false;ctx.now_ms+=1000;
    assert(recover_library_card(&ctx)==APP_REDRAW_PAGE && !s_card_missing);
    puts("Metalio reader HOME/settings/refresh/frontlight: PASS");
}
