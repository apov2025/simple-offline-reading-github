/* SPDX-License-Identifier: Apache-2.0
 * Metalio 开机装配；没有已核实校准时禁止上屏高压。
 * Metalio boot wiring; prevent panel high voltage without verified calibration.
 */
#include "metalio_board.h"
#include "reader_media.h"
#include "reader_waveform.h"
#include "app_loop.h"
#include "app_registry.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "epd_board_metalio_eink4_plus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef METALIO_VCOM_MV
#define METALIO_VCOM_MV 0
#endif
void app_main(void) {
    ESP_LOGI("metalio_reader","boot: hardware profile %s; calibration %d mV",METALIO_PIN_PROFILE,METALIO_VCOM_MV);
    if(METALIO_VCOM_MV<1000 || METALIO_VCOM_MV>3000 || METALIO_VCOM_MV%10) {
        ESP_LOGE("metalio_reader","No verified panel calibration; display disabled");return;
    }
    app_settings_init();
    metalio_reader_sleep_init();
    epd_init(&epd_board_metalio_eink4_plus,&E0470A14_AF,EPD_LUT_1K);
    epd_set_vcom(METALIO_VCOM_MV);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    my_waveform_init();
    EpdiyHighlevelState hl=epd_hl_init(&MY_WAVEFORM);
    if(!epd_board_metalio_eink4_plus_ioexp_ok() || metalio_reader_init()!=ESP_OK) {
        ESP_LOGE("metalio_reader","Board input initialization failed");return;
    }
    reader_media_start_probe();
    // 首帧前有界等待卡，避免字体先后跳变；失败仍能读内置书。
    // Bound the first-frame card wait to avoid font swaps; failures retain built-in reading.
    for(int i=0;i<80;i++) {
        reader_media_info_t media;
        if(reader_media_get_info(&media)!=ESP_ERR_NOT_FINISHED)break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ttf_font_init();
    epd_poweron();
    if(!epd_board_metalio_eink4_plus_tps_ok()){ESP_LOGE("metalio_reader","Panel power failed");epd_poweroff();return;}
    enum EpdDrawError cleared=epd_clear_checked();
    epd_poweroff();
    if(cleared!=EPD_DRAW_SUCCESS){ESP_LOGE("metalio_reader","Initial panel clear failed: %d",cleared);return;}
    epd_hl_set_all_white(&hl);
    reader_board_set_light(app_settings_light_level(),app_settings_light_warmth());
    app_loop_run(&(app_loop_config_t){.hl=&hl,.fb=epd_hl_get_framebuffer(&hl),.first_app=app_reader_page()});
}
