/* SPDX-License-Identifier: Apache-2.0
 * 测试真实 Metalio 睡眠循环和恢复顺序。/ Test the actual Metalio sleep loop and restore order.
 */
#define READER_METALIO 1
#include "esp_sleep.h"
#include "display.h"
#include "ui_kit.h"
#include "ui_power_screen.h"
#include <assert.h>
int test_gpio_level(gpio_num_t pin);
int64_t test_time(void);
void test_delay(TickType_t ticks);
esp_err_t test_gpio_wake(gpio_num_t pin,gpio_int_type_t type);
esp_err_t test_enable_wake(void);
esp_err_t test_ext1_wake(uint64_t mask,esp_sleep_ext1_wakeup_mode_t mode);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source);
uint8_t* test_framebuffer(EpdiyHighlevelState* hl);
void test_power_screen(uint8_t* fb,ui_power_screen_t state,const uint8_t* cover);
void test_image(uint8_t* fb,const uint8_t* image);
enum EpdDrawError test_refresh(EpdiyHighlevelState* hl);
void test_poweroff(void);
void test_guard_result(EpdiyHighlevelState* hl,enum EpdDrawError err);
#define rtc_gpio_hold_dis test_rtc_hold_dis
#define rtc_gpio_deinit test_rtc_deinit
#define gpio_get_level test_gpio_level
#define gpio_wakeup_enable test_gpio_wake
#define esp_sleep_enable_gpio_wakeup test_enable_wake
#define esp_sleep_enable_ext1_wakeup_io test_ext1_wake
#define esp_timer_get_time test_time
#define vTaskDelay test_delay
#define metalio_reader_power_level test_power_level
#define metalio_reader_light_wait test_light_wait
#define epd_hl_get_framebuffer test_framebuffer
#define ui_draw_full_image test_image
#define ui_draw_power_screen test_power_screen
#define update_display_full test_refresh
#define epd_poweroff test_poweroff
#define guard_draw_result test_guard_result
#include "../../main/board/metalio_sleep.c"
static int sleeps, key_after, reads, restores, irq_reads, low_irqs, pulses, lock_screens, off_screens, fail_screens;
static gpio_int_type_t normal_irq_type;
static bool irq_starved, ext1_armed, rtc_owned, rtc_held, sleep_fail, wake_hold;
static int64_t release_at;
static int guards, refreshes, fail_on_refresh;
static bool down, broken, media_busy, wake_pin_ok, gpio_fail, wake_fail, refresh_fail, lifecycle, resume_phase;
static unsigned probes, delays;
static int64_t ticks;
static char events[64];
static unsigned event_count;
static void event(char value){assert(event_count+1<sizeof(events));events[event_count++]=value;events[event_count]=0;}
int64_t test_time(void){return ticks*1000;}
void test_delay(TickType_t value){ticks+=value;assert(++delays<600);}
// SDK gpio_wakeup_enable 同时改普通中断类型；TCA ISR 只通知，读卡口在任务内。
// SDK gpio_wakeup_enable also changes the normal IRQ type; TCA input acknowledgement runs in task context.
esp_err_t test_gpio_wake(gpio_num_t pin,gpio_int_type_t type){assert(pin==2);normal_irq_type=type;return gpio_fail?ESP_FAIL:ESP_OK;}
esp_err_t test_ext1_wake(uint64_t mask,esp_sleep_ext1_wakeup_mode_t mode){
    assert(mask==(1ULL<<2) && mode==ESP_EXT1_WAKEUP_ANY_LOW);
    if(gpio_fail||wake_fail)return ESP_FAIL;
    ext1_armed=true;return ESP_OK;
}
esp_err_t test_enable_wake(void){return wake_fail?ESP_FAIL:ESP_OK;}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source){
    if(source==ESP_SLEEP_WAKEUP_EXT1)ext1_armed=false;
    return ESP_OK;
}
esp_err_t test_rtc_hold_dis(gpio_num_t pin){assert(pin==2);rtc_held=false;return ESP_OK;}
esp_err_t test_rtc_deinit(gpio_num_t pin){assert(pin==2 && !rtc_held);rtc_owned=false;return ESP_OK;}
esp_err_t reader_media_get_info(reader_media_info_t* info) {
    probes++;*info=(reader_media_info_t){.error=media_busy?ESP_ERR_NOT_FINISHED:ESP_OK};return info->error;
}
uint8_t* test_framebuffer(EpdiyHighlevelState* hl){(void)hl;assert(!media_busy);return NULL;}
void test_power_screen(uint8_t* fb,ui_power_screen_t state,const uint8_t* cover){
 (void)fb;(void)cover;
 if(state==UI_POWER_LOCKED){lock_screens++;event('D');}
 else if(state==UI_POWER_SHUTTING_DOWN){off_screens++;event('Q');}
 else {fail_screens++;event('E');}
}
void epd_board_metalio_eink4_plus_pwr_key_pulse_train(void){assert(off_screens==1 && events[event_count-1]=='O' && !media_busy);pulses++;down=false;event('P');}
void test_image(uint8_t* fb,const uint8_t* image){(void)fb;(void)image;event('D');}
enum EpdDrawError test_refresh(EpdiyHighlevelState* hl){(void)hl;event('F');refreshes++;return (refresh_fail || refreshes==fail_on_refresh)?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS;}
void test_poweroff(void){event('O');}
void test_guard_result(EpdiyHighlevelState* hl,enum EpdDrawError err){(void)hl;assert(err!=EPD_DRAW_SUCCESS);guards++;}
esp_err_t epd_board_metalio_eink4_plus_ioexp_mask_for_sleep(void){event('M');return ESP_OK;}
bool epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin(void){return wake_pin_ok;}
esp_err_t epd_board_metalio_eink4_plus_ioexp_restore_keys(void){restores++;event('K');return ESP_OK;}
void epd_board_metalio_eink4_plus_bus_hold_low(void){event('H');}
void epd_board_metalio_eink4_plus_bus_latch_for_sleep(void){event('L');}
void epd_board_metalio_eink4_plus_bus_restore(void){resume_phase=true;event('R');}
void metalio_reader_clear_keys(void){event('C');}
void metalio_reader_light_sleep(bool value){event(value?'0':'1');}
int test_gpio_level(gpio_num_t pin){(void)pin;assert(!rtc_owned && !rtc_held);irq_reads++;return irq_reads<=low_irqs?0:1;}
esp_err_t test_power_level(bool* out) {
    assert(++reads<=700);
    if(broken)return ESP_FAIL;
    if(release_at>=0 && ticks>=release_at)down=false;
    *out=lifecycle && resume_phase && !wake_hold?false:down;return ESP_OK;
}
esp_err_t test_light_wait(void) {
    assert(!down && !gpio_fail && !wake_fail);
    assert(++sleeps<=3);event('S');
    if(ext1_armed)rtc_owned=rtc_held=true;
    if(sleep_fail)return ESP_FAIL;
    if(sleeps==key_after)down=true;
    // 持续低电平重触发普通 ISR，任务无法读 TCA 清中断；模型不证明芯片时序。
    // An asserted level IRQ can starve the task that acknowledges TCA; this is not a silicon timing simulation.
    if(down && normal_irq_type==GPIO_INTR_LOW_LEVEL){irq_starved=true;return ESP_FAIL;}
    return ESP_OK;
}
static void reset(void){
    sleeps=reads=restores=irq_reads=low_irqs=pulses=lock_screens=off_screens=fail_screens=0;probes=delays=0;ticks=0;
    down=broken=media_busy=gpio_fail=wake_fail=refresh_fail=lifecycle=resume_phase=false;
    wake_pin_ok=true;key_after=1;event_count=0;events[0]=0;normal_irq_type=GPIO_INTR_NEGEDGE;irq_starved=ext1_armed=rtc_owned=rtc_held=sleep_fail=wake_hold=false;release_at=-1;guards=refreshes=fail_on_refresh=0;
}
int main(void){
    reset();down=true;assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY && sleeps==0 && restores==1);
    reset();assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY && sleeps==1 && restores==1);
    assert(!irq_starved && !ext1_armed && !rtc_owned && !rtc_held && normal_irq_type==GPIO_INTR_NEGEDGE);
    down=false;key_after=2;assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY && sleeps==2 && restores==2);
    assert(!irq_starved && !ext1_armed && !rtc_owned && !rtc_held && normal_irq_type==GPIO_INTR_NEGEDGE);
    reset();key_after=2;assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY && sleeps==2 && restores==1);
    reset();low_irqs=3;assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY && irq_reads==4 && sleeps==1 && delays>=4);
    reset();broken=true;assert(app_light_sleep_wait(NULL)==APP_WAKE_NONE && restores==1 && sleeps==0);
    reset();wake_pin_ok=false;assert(app_light_sleep_wait(NULL)==APP_WAKE_NONE && restores==1 && sleeps==0);
    reset();gpio_fail=true;assert(app_light_sleep_wait(NULL)==APP_WAKE_NONE && restores==1 && sleeps==0);
    reset();wake_fail=true;assert(app_light_sleep_wait(NULL)==APP_WAKE_NONE && restores==1 && sleeps==0);
    reset();sleep_fail=true;
    assert(app_light_sleep_wait(NULL)==APP_WAKE_NONE && sleeps==1 && restores==1);
    assert(!ext1_armed && !rtc_owned && !rtc_held && normal_irq_type==GPIO_INTR_NEGEDGE);
    reset();media_busy=true;int64_t ignore=0;
    enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(probes==20 && sleeps==0 && ignore==0 && events[0]==0);
    reset();lifecycle=true;enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(strcmp(events,"DFO0HLMSKRC1")==0 && sleeps==1 && ignore==ticks+2000);
    reset();refresh_fail=true;ignore=0;enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(strcmp(events,"DFO")==0 && sleeps==0 && ignore==0);
    reset();down=true;lifecycle=true;ignore=0;
    enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(pulses==1 && off_screens==1 && fail_screens==1 && lock_screens==0 && sleeps==0);
    reset();down=true;release_at=200;
    assert(power_press_kind()==0 && ticks==200 && pulses==0);
    reset();down=true;release_at=2980;assert(power_press_kind()==0 && pulses==0);
    reset();down=true;assert(power_press_kind()==1 && ticks==3000 && pulses==0);
    reset();broken=true;assert(power_press_kind()==-1 && pulses==0);
    reset();down=true;refresh_fail=true;enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(off_screens==1 && pulses==0 && fail_screens==0 && lock_screens==0 && guards==1);
    reset();down=true;fail_on_refresh=2;enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(pulses==1 && fail_screens==1 && guards==1);
    reset();lifecycle=true;wake_hold=true;enter_lock_and_sleep(NULL,&ignore,NULL);
    assert(lock_screens==1 && off_screens==1 && fail_screens==1 && pulses==1 && sleeps==1);
    reset();broken=true;enter_lock_and_sleep(NULL,&ignore,NULL);assert(pulses==0 && lock_screens==0);
    reset();assert(app_enter_host_sleep(APP_SLEEP_DEEP)==ESP_ERR_NOT_SUPPORTED && pulses==0);
    puts("Metalio sleep: pending/held key, shared IRQ, I2C/wake errors, busy card and lifecycle order PASS");
}

// 宿主测试混用 Pico 主循环对象；无名生产主循环不依赖此查询。
// The host harness links the Pico loop; the Metalio production loop has no such dependency.
bool app_lock_has_image(void) { return false; }
