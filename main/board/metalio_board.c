/* SPDX-License-Identifier: Apache-2.0
 * Metalio 阅读器触摸、实体键和双色温前光；不启用相机或4G。
 * Metalio reader touch, physical keys and dual-temperature frontlight; no camera or 4G activation.
 */
#include "metalio_board.h"
#include "reader_controls.h"
#include "settings.h"
#include "metalio_i2c1.h"
#include "epd_board_metalio_eink4_plus.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include <string.h>

static reader_key_state_t keys;
static uint8_t pending;
static bool sleeping;
static esp_err_t ft_read(uint8_t reg, uint8_t* data, size_t n) {
    if (!metalio_i2c1_lock(25)) return ESP_ERR_TIMEOUT;
    esp_err_t err=metalio_i2c1_transmit_receive_locked(I2C_NUM_1,0x38,&reg,1,data,n,25);
    metalio_i2c1_unlock(); return err;
}
esp_err_t reader_touch_get_info(reader_touch_handle_t h,reader_touch_info_t* info) {
    (void)h; memset(info,0,sizeof(*info)); return ft_read(0xa6,&info->fw,1);
}
esp_err_t reader_touch_read(reader_touch_handle_t h,reader_touch_t* touch) {
    (void)h; memset(touch,0,sizeof(*touch));
    uint8_t raw[13]; esp_err_t err=ft_read(2,raw,sizeof(raw));
    if(err!=ESP_OK) return err;
    unsigned count=raw[0]&15;
    if (!count) return ESP_OK;
    if (count>2) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<count;i++) {
        const uint8_t* p=raw+1+i*6;
        if ((p[0]>>6)==1) return ESP_OK;
        uint16_t x=((p[0]&15)<<8)|p[1], y=((p[2]&15)<<8)|p[3];
        if(!reader_controls_touch_xy(x,y,count,&x,&y))return ESP_ERR_INVALID_RESPONSE;
        touch->points[i]=(reader_touch_point_t){.active=true,.id=p[2]>>4,.event=p[0]>>6,.x=x,.y=y};
    }
    touch->touched=true;touch->count=count;touch->x=touch->points[0].x;touch->y=touch->points[0].y;
    memcpy(touch->raw,raw,sizeof(raw));return ESP_OK;
}
static void sample_keys(void) {
    uint8_t p=255;
    bool valid=epd_board_metalio_eink4_plus_ioexp_read_port(1,&p);
    uint8_t bits=(!(p&128)) | ((!(p&1))<<1) | ((!(p&2))<<2) | ((!gpio_get_level(61))<<3);
    pending |= reader_controls_side_update(&keys,valid,bits,esp_timer_get_time()/1000);
}
bool reader_board_power_ready(void) { return epd_board_metalio_eink4_plus_ioexp_ok(); }
bool reader_board_take_power(void) { sample_keys(); bool have=pending&1;pending&=~1;return have; }
int reader_board_take_side_key(void) {
    sample_keys();
    const int codes[]={READER_INPUT_PREVIOUS,READER_INPUT_NEXT,READER_INPUT_SCREENSHOT};
    for(unsigned i=0;i<3;i++) if(pending&(2<<i)) {pending&=~(2<<i);return codes[i];}
    return -1;
}
esp_err_t metalio_reader_power_level(bool* down) {
    uint8_t p;
    if(!epd_board_metalio_eink4_plus_ioexp_read_port(1,&p))return ESP_ERR_TIMEOUT;
    *down=!(p&128);return ESP_OK;
}
void metalio_reader_clear_keys(void) { memset(&keys,0,sizeof(keys));pending=0;sample_keys(); }
void reader_board_set_light(uint8_t level,uint8_t warmth) {
    // 官方前光最大仅为10bit PWM的10%；UI亮度不是硬件满占空。
    // Vendor frontlight caps 10-bit PWM at 10%; UI brightness is not full hardware duty.
    const uint32_t max_duty=1023/10;
    uint16_t cool,warm;reader_controls_light(level,warmth,sleeping,&cool,&warm);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,(uint32_t)cool*max_duty/1000);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,(uint32_t)warm*max_duty/1000);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);
}
void metalio_reader_light_sleep(bool asleep) {
    sleeping=asleep;reader_board_set_light(app_settings_light_level(),app_settings_light_warmth());
}
esp_err_t metalio_reader_init(void) {
    gpio_config_t cfg={.pin_bit_mask=(1ULL<<5)|(1ULL<<61),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    esp_err_t err=gpio_config(&cfg);if(err!=ESP_OK)return err;
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,.timer_num=LEDC_TIMER_0,.freq_hz=20000,.clk_cfg=LEDC_AUTO_CLK};
    err=ledc_timer_config(&timer);if(err!=ESP_OK)return err;
    for(int i=0;i<2;i++) {
        ledc_channel_config_t ch={.gpio_num=6+i,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=i,.timer_sel=LEDC_TIMER_0,.duty=0};
        err=ledc_channel_config(&ch);if(err!=ESP_OK)return err;
    }
    epd_board_metalio_eink4_plus_tp_reset();
    reader_touch_info_t info;
    err=reader_touch_get_info(NULL,&info);
    metalio_reader_clear_keys();return err;
}
