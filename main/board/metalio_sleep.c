/* SPDX-License-Identifier: Apache-2.0
 * Metalio 浅睡保留 SD 供电与句柄，不模拟 Pico PMU；共享 IRQ 只检查真实电源键。
 * Metalio light sleep retains SD power and handles, without Pico PMU emulation; shared IRQ checks the actual power key.
 */
#include "sleep.h"
#include "metalio_board.h"
#include "reader_media.h"
#include "display.h"
#include "ui_kit.h"
#include "ui_power_screen.h"
#include "epd_board_metalio_eink4_plus.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const uint8_t* lock_image;
void app_sleep_bind(reader_touch_handle_t tp,sc7a20h_handle_t acc){(void)tp;(void)acc;}
bool app_lock_powers_down(void){return true;}
void app_lock_set_image(const uint8_t* image){lock_image=image;}
app_wake_source_t app_last_wake_source(void){return app_settings_last_wake();}
void app_lock_wait_key_idle(int timeout_ms) {
    int64_t end=esp_timer_get_time()/1000+timeout_ms;
    unsigned released=0;
    while(esp_timer_get_time()/1000<end) {
        bool down;
        if(metalio_reader_power_level(&down)==ESP_OK && !down){if(++released>=3)return;}else released=0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    (void)acc;
    bool masked=epd_board_metalio_eink4_plus_ioexp_mask_for_sleep()==ESP_OK;
    if(!masked || !epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin()) {
        epd_board_metalio_eink4_plus_ioexp_restore_keys();return APP_WAKE_NONE;
    }
    // EXT1 不改 GPIO2 已注册 ISR 的下降沿类型；普通 GPIO 唤醒会将其改成低电平。
    // EXT1 preserves GPIO2's installed falling-edge ISR; GPIO wake would change it to level-triggered.
    if(esp_sleep_enable_ext1_wakeup_io(1ULL<<GPIO_NUM_2,ESP_EXT1_WAKEUP_ANY_LOW)!=ESP_OK) {
        epd_board_metalio_eink4_plus_ioexp_restore_keys();return APP_WAKE_NONE;
    }
    bool woke=false;
    unsigned read_errors=0;
    for(;;) {
        bool down=false;
        esp_err_t sample=metalio_reader_power_level(&down);
        if(sample!=ESP_OK) {
            if(++read_errors>=3)break;
            vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
        read_errors=0;
        if(down) {
            vTaskDelay(pdMS_TO_TICKS(30));
            sample=metalio_reader_power_level(&down);
            if(sample==ESP_OK && down){woke=true;break;}
            if(sample!=ESP_OK){read_errors=1;continue;}
        }
        // 连续低IRQ不能强行解锁；退让CPU后重读按键。
        // A persistently low shared IRQ cannot unlock; yield then sample the key again.
        if(!gpio_get_level(GPIO_NUM_2)){vTaskDelay(pdMS_TO_TICKS(20));continue;}
        esp_err_t err=metalio_reader_light_wait();
        // EXT1 将引脚交给 RTC；再次读普通 GPIO 前解除保持并归还数字输入。
        // EXT1 routes the pad to RTC; release hold and restore digital routing before GPIO reads.
        esp_err_t hold_err=rtc_gpio_hold_dis(GPIO_NUM_2);
        esp_err_t mux_err=rtc_gpio_deinit(GPIO_NUM_2);
        if(err==ESP_OK)err=hold_err;
        if(err==ESP_OK)err=mux_err;
        if(err!=ESP_OK){ESP_LOGW("metalio_sleep","sleep rejected: %s",esp_err_to_name(err));break;}
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    epd_board_metalio_eink4_plus_ioexp_restore_keys();
    return woke?APP_WAKE_KEY:APP_WAKE_NONE;
}
// 只使用官方关机脉冲协议；请求后CPU仍在运行即按失败处理。
// Use only the vendor shutdown pulse protocol; a still-running CPU means failure.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    if(mode!=APP_SLEEP_OFF)return ESP_ERR_NOT_SUPPORTED;
    epd_board_metalio_eink4_plus_pwr_key_pulse_train();
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_FAIL;
}
// 返回0为已松开/空闲，1为连续长按，-1为采样失败；失败不能触发关机。
// Return 0 for release/idle, 1 for a continuous hold, -1 for sampling failure; errors cannot power off.
static int power_press_kind(void) {
    const int64_t start=esp_timer_get_time()/1000;
    for(;;) {
        bool down=false;
        if(metalio_reader_power_level(&down)!=ESP_OK)return -1;
        if(!down)return 0;
        if(esp_timer_get_time()/1000-start>=3000)return 1;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static void request_shutdown_screen(EpdiyHighlevelState* hl) {
    // 调用前页面已保存进度，挂载任务已结束；不卸载仍有只读句柄的SD。
    // Page exit already saved progress and mount is idle; do not unmount SD with live read handles.
    metalio_reader_light_sleep(true);
    ui_draw_power_screen(epd_hl_get_framebuffer(hl),UI_POWER_SHUTTING_DOWN,NULL);
    enum EpdDrawError drawn=update_display_full(hl);
    epd_poweroff();
    if(drawn==EPD_DRAW_SUCCESS)(void)app_enter_host_sleep(APP_SLEEP_OFF);
    else guard_draw_result(hl,drawn);
    // 真断电不会执行到这里；不能把未完成的请求标为“已关机”。
    // Actual power-off never reaches this point; never label an incomplete request as powered off.
    ESP_LOGW("metalio_sleep","shutdown incomplete; return to reader");
    if(drawn==EPD_DRAW_SUCCESS) {
        ui_draw_power_screen(epd_hl_get_framebuffer(hl),UI_POWER_FAILED,NULL);
        enum EpdDrawError failed_draw=update_display_full(hl);
        epd_poweroff();
        if(failed_draw!=EPD_DRAW_SUCCESS)guard_draw_result(hl,failed_draw);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
void enter_lock_and_sleep(EpdiyHighlevelState* hl,int64_t* ignore_until,sc7a20h_handle_t acc) {
    // 挂载任务仍在操作控制器时不进入睡眠，也不强制卸载。
    // Do not sleep or force-unmount while the mount task owns the controller.
    bool media_idle=false;
    for(unsigned attempt=0;attempt<20;attempt++) {
        reader_media_info_t info;
        if(reader_media_get_info(&info)!=ESP_ERR_NOT_FINISHED){media_idle=true;break;}
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if(!media_idle){ESP_LOGW("metalio_sleep","card busy; lock deferred");return;}

    int press=power_press_kind();
    if(press<0){ESP_LOGW("metalio_sleep","power key read failed; lock cancelled");return;}
    if(press==1) {
        request_shutdown_screen(hl);
        app_lock_wait_key_idle(800);metalio_reader_clear_keys();metalio_reader_light_sleep(false);
        if(ignore_until)*ignore_until=esp_timer_get_time()/1000+2000;
        return;
    }
    ui_draw_power_screen(epd_hl_get_framebuffer(hl),UI_POWER_LOCKED,
        lock_image);
    enum EpdDrawError drawn=update_display_full(hl);
    epd_poweroff();
    if(drawn!=EPD_DRAW_SUCCESS){
        ESP_LOGW("metalio_sleep","lock refresh failed: %d",drawn);
        guard_draw_result(hl,drawn);return;
    }
    metalio_reader_light_sleep(true);
    app_lock_wait_key_idle(1000);
    epd_board_metalio_eink4_plus_bus_hold_low();
    epd_board_metalio_eink4_plus_bus_latch_for_sleep();
    app_wake_source_t wake=app_light_sleep_wait(acc);
    epd_board_metalio_eink4_plus_bus_restore();
    if(wake==APP_WAKE_KEY && power_press_kind()==1)request_shutdown_screen(hl);
    app_settings_set_last_wake(wake);
    app_lock_wait_key_idle(800);metalio_reader_clear_keys();metalio_reader_light_sleep(false);
    if(ignore_until)*ignore_until=esp_timer_get_time()/1000+2000;
    ESP_LOGI("metalio_sleep","resume: wake=%d",wake);
}
