/* SPDX-License-Identifier: Apache-2.0
 * 真实设置实现的提交失败与重启恢复。/ Commit failure and reboot recovery of the real settings implementation.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/settings.c"
static char stored[2][96],pending[96];
static int pending_key=-1,handles,commits;
static bool fail_write,fail_commit;
esp_err_t nvs_flash_init(void) { return 0; }
esp_err_t nvs_flash_erase(void) { assert(0);return -1; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) { (void)ns;(void)mode;*h=1;++handles;return 0; }
void nvs_close(nvs_handle_t h) { (void)h;--handles;pending_key=-1; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *value) { (void)h;(void)key;(void)value;return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value) { (void)h;(void)key;(void)value;return 0; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *value,size_t *size) {
    (void)h;int which=!strcmp(key,"wp_lock")?0:!strcmp(key,"wp_off")?1:-1;
    if(which<0 || !stored[which][0])return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>strlen(stored[which]));strcpy(value,stored[which]);return 0;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *value) {
    (void)h;if(fail_write)return ESP_FAIL;
    pending_key=!strcmp(key,"wp_lock")?0:!strcmp(key,"wp_off")?1:-1;assert(pending_key>=0);strcpy(pending,value);return 0;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;++commits;if(fail_commit)return ESP_FAIL;assert(pending_key>=0);strcpy(stored[pending_key],pending);return 0; }
int main(void) {
    app_settings_init();assert(!*app_settings_wallpaper_last(false));
    assert(app_settings_set_wallpaper_last(false,"001.4bpp") && commits==1);
    assert(app_settings_set_wallpaper_last(false,"001.4bpp") && commits==1);
    assert(app_settings_set_wallpaper_last(true,"007.4bpp") && commits==2);
    memset(s_wallpaper,0,sizeof(s_wallpaper));app_settings_init();
    assert(!strcmp(app_settings_wallpaper_last(false),"001.4bpp") && !strcmp(app_settings_wallpaper_last(true),"007.4bpp"));
    fail_write=true;assert(!app_settings_set_wallpaper_last(false,"002.4bpp"));fail_write=false;
    fail_commit=true;assert(!app_settings_set_wallpaper_last(false,"003.4bpp"));fail_commit=false;
    assert(!strcmp(app_settings_wallpaper_last(false),"001.4bpp") && !strcmp(stored[0],"001.4bpp"));
    assert(!app_settings_set_wallpaper_last(false,"../bad.4bpp"));
    assert(app_settings_set_wallpaper_last(false,"004.4bpp"));
    memset(s_wallpaper,0,sizeof(s_wallpaper));app_settings_init();assert(!strcmp(app_settings_wallpaper_last(false),"004.4bpp") && !handles);
    puts("wallpaper NVS: independent lock/off, restart recovery, no duplicate write, write/commit failure preserves cursor PASS");
}
