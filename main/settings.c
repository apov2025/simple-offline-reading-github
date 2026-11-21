/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 读写。打开失败就用关机默认值，不擦除整个分区。
 *
 * NVS load/store. A failed open keeps the power-off default; the
 * partition is not erased.
 */

#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "settings"
#define NVS_NS "read_pico"
#define NVS_KEY_SLEEP "sleep"
#define NVS_KEY_FONT "font"
#define NVS_KEY_WAKE "lwake"
#define NVS_KEY_BOOT "lboot"
#define NVS_KEY_PICKUP "pickup"
#define NVS_KEY_BOOK_PX "bk_px"
#define NVS_KEY_BOOK_SHAKE "bk_shake"
#define NVS_KEY_READER_REFRESH "rd_refresh"
#define NVS_KEY_FONT_PICKED "rd_fontpick"
#define FONT_PATH_MAX 160

// 阅读器默认软休眠：单击电源键锁屏，再单击唤醒；休眠时主控断电。唤醒是重新开机，
// 阅读器会自动回到锁屏前读的那本书。官方演示固件的默认是关机（要长按才开机）。
// The reader defaults to soft sleep: one press of the power key locks, another wakes; the host is
// unpowered while asleep. Waking is a fresh boot, and the reader reopens the book that was open.
// The vendor demo defaults to power-off, which needs a long press to boot.
static app_sleep_mode_t s_sleep = APP_SLEEP_DEEP;
static char s_font[FONT_PATH_MAX];
static uint8_t s_last_wake;
static uint8_t s_last_boot;
static bool s_pickup_wake;
static uint8_t s_book_px = 48;
static bool s_book_shake;
static uint8_t s_reader_refresh = APP_READER_REFRESH_FAST;
static bool s_font_picked;
#if !READER_METALIO
static char s_wallpaper[2][96];
#endif
static uint8_t s_light_level;
static uint8_t s_light_warmth = 1;

static uint8_t valid_book_px(uint8_t px) {
    return px >= 36 && px <= 72 && (px - 36) % 4 == 0 ? px : 48;
}

void app_settings_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init %s, use power off", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t raw = APP_SLEEP_DEEP;
    if (nvs_get_u8(h, NVS_KEY_SLEEP, &raw) == ESP_OK && raw <= APP_SLEEP_OFF) {
        s_sleep = (app_sleep_mode_t)raw;
    }
    size_t font_len = sizeof(s_font);
    if (nvs_get_str(h, NVS_KEY_FONT, s_font, &font_len) != ESP_OK) {
        s_font[0] = '\0';
    }
    uint8_t wake = 0;
    if (nvs_get_u8(h, NVS_KEY_WAKE, &wake) == ESP_OK) s_last_wake = wake;
    uint8_t boot = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOT, &boot) == ESP_OK) s_last_boot = boot;
    uint8_t pickup = 0;
    if (nvs_get_u8(h, NVS_KEY_PICKUP, &pickup) == ESP_OK) s_pickup_wake = pickup != 0;
    uint8_t book_px = 48, book_shake = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOK_PX, &book_px) == ESP_OK) s_book_px = valid_book_px(book_px);
    if (nvs_get_u8(h, NVS_KEY_BOOK_SHAKE, &book_shake) == ESP_OK) s_book_shake = book_shake != 0;
    uint8_t reader_refresh = APP_READER_REFRESH_FAST;
    if (nvs_get_u8(h, NVS_KEY_READER_REFRESH, &reader_refresh) == ESP_OK
        && reader_refresh <= APP_READER_REFRESH_CLEAR) {
        s_reader_refresh = reader_refresh;
    }
    uint8_t font_picked = 0;
    if (nvs_get_u8(h, NVS_KEY_FONT_PICKED, &font_picked) == ESP_OK) s_font_picked = font_picked != 0;
    uint8_t light = 0, warmth = 1;
    if (nvs_get_u8(h, "fl_level", &light) == ESP_OK && light <= 3) s_light_level = light;
    if (nvs_get_u8(h, "fl_warmth", &warmth) == ESP_OK && warmth <= 2) s_light_warmth = warmth;
#if !READER_METALIO
    for(int i=0;i<2;i++) {
        size_t length=sizeof(s_wallpaper[i]);
        if(nvs_get_str(h,i?"wp_off":"wp_lock",s_wallpaper[i],&length)!=ESP_OK)s_wallpaper[i][0]=0;
    }
#endif
    nvs_close(h);
    ESP_LOGI(
        TAG, "sleep mode %s, font %s",
        app_sleep_mode_name(s_sleep),
        s_font[0] != '\0' ? s_font : "(builtin)"
    );
}

app_sleep_mode_t app_settings_sleep_mode(void) {
    return s_sleep;
}

const char* app_sleep_mode_name(app_sleep_mode_t mode) {
    switch (mode) {
        case APP_SLEEP_LIGHT: return "light";
        case APP_SLEEP_DEEP: return "deep";
        case APP_SLEEP_OFF: return "off";
        default: return "?";
    }
}

void app_settings_set_sleep_mode(app_sleep_mode_t mode) {
    if (mode > APP_SLEEP_OFF) mode = APP_SLEEP_OFF;
    s_sleep = mode;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_SLEEP, (uint8_t)mode);
    nvs_commit(h);
    nvs_close(h);
}

const char* app_settings_font_path(void) {
    return s_font;
}

static void nvs_put_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_last_wake(void) {
    return s_last_wake;
}

void app_settings_set_last_wake(uint8_t src) {
    if (src == s_last_wake) return;
    s_last_wake = src;
    nvs_put_u8(NVS_KEY_WAKE, src);
}

uint8_t app_settings_last_boot(void) {
    return s_last_boot;
}

void app_settings_set_last_boot(uint8_t reason) {
    if (reason == 0 || reason == s_last_boot) return;
    s_last_boot = reason;
    nvs_put_u8(NVS_KEY_BOOT, reason);
}

bool app_settings_pickup_wake(void) {
    return s_pickup_wake;
}

void app_settings_set_pickup_wake(bool on) {
    if (s_pickup_wake == on) return;
    s_pickup_wake = on;
    nvs_put_u8(NVS_KEY_PICKUP, on ? 1 : 0);
}

void app_settings_set_font_path(const char* path) {
    if (path == NULL) path = "";
    strlcpy(s_font, path, sizeof(s_font));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_FONT, s_font);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_book_px(void) {
    return s_book_px;
}

void app_settings_set_book_px(uint8_t px) {
    px = valid_book_px(px);
    if (s_book_px == px) return;
    s_book_px = px;
    nvs_put_u8(NVS_KEY_BOOK_PX, px);
}

app_reader_refresh_t app_settings_reader_refresh(void) {
    return (app_reader_refresh_t)s_reader_refresh;
}

void app_settings_set_reader_refresh(app_reader_refresh_t mode) {
    if (mode > APP_READER_REFRESH_CLEAR) mode = APP_READER_REFRESH_FAST;
    if (s_reader_refresh == (uint8_t)mode) return;
    s_reader_refresh = (uint8_t)mode;
    nvs_put_u8(NVS_KEY_READER_REFRESH, (uint8_t)mode);
}

bool app_settings_font_user_picked(void) {
    return s_font_picked;
}

void app_settings_set_font_user_picked(bool picked) {
    if (s_font_picked == picked) return;
    s_font_picked = picked;
    nvs_put_u8(NVS_KEY_FONT_PICKED, picked ? 1 : 0);
}

bool app_settings_book_shake(void) {
    return s_book_shake;
}

void app_settings_set_book_shake(bool on) {
    if (s_book_shake == on) return;
    s_book_shake = on;
    nvs_put_u8(NVS_KEY_BOOK_SHAKE, on ? 1 : 0);
}

uint8_t app_settings_light_level(void) { return s_light_level; }
uint8_t app_settings_light_warmth(void) { return s_light_warmth; }
void app_settings_set_light(uint8_t level, uint8_t warmth) {
    level = level <= 3 ? level : 0;
    warmth = warmth <= 2 ? warmth : 1;
    if (level != s_light_level) nvs_put_u8("fl_level", level);
    if (warmth != s_light_warmth) nvs_put_u8("fl_warmth", warmth);
    s_light_level = level;
    s_light_warmth = warmth;
}

#if !READER_METALIO
const char *app_settings_wallpaper_last(bool off) { return s_wallpaper[off]; }
bool app_settings_set_wallpaper_last(bool off,const char *name) {
    if(!name || strlen(name)>=sizeof(s_wallpaper[off]) || strchr(name,'/'))return false;
    if(!strcmp(name,s_wallpaper[off]))return true;
    nvs_handle_t h;if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t error=nvs_set_str(h,off?"wp_off":"wp_lock",name);
    if(error==ESP_OK)error=nvs_commit(h);
    nvs_close(h);
    if(error!=ESP_OK)return false;
    strcpy(s_wallpaper[off],name);return true;
}

#endif
