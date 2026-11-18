/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 板级硬件替身：假 I2C 总线、墨水屏板定义、触摸、电源芯片（PMU）、TF 卡、
 * 蜂鸣器、传书。固件里的 read_pico_init.c、sc7a20h.c、sy7636a.c 等原样编译，
 * 落到这里提供的最底层接口上。
 *
 * Board stand-ins: fake I2C bus, EPD board definition, touch, PMU, TF card,
 * buzzer, transfer. read_pico_init.c, sc7a20h.c, sy7636a.c etc. are compiled
 * as-is and land on the lowest-level entry points provided here.
 */
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "cst836u.h"
#include "epdiy.h"
#include "read_pico_board.h"
#include "read_pico_buzzer.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "sim.h"
#include "sim_idf.h"

static const char* TAG = "sim_board";

void sim_epd_rails(bool on);  // sim_epd.c

/* ---- 假 I2C：每个设备一张寄存器表 / fake I2C: a register file per device ---- */

struct sim_i2c_bus { int unused; };
struct sim_i2c_dev {
    uint16_t addr;
    uint8_t regs[256];
    uint8_t ptr;
};

static struct sim_i2c_bus s_bus;

i2c_master_bus_handle_t read_pico_i2c_bus(void) { return &s_bus; }

esp_err_t i2c_master_bus_add_device(
    i2c_master_bus_handle_t bus, const i2c_device_config_t* cfg, i2c_master_dev_handle_t* out
) {
    (void)bus;
    struct sim_i2c_dev* dev = calloc(1, sizeof(*dev));
    if (dev == NULL) return ESP_ERR_NO_MEM;
    dev->addr = cfg->device_address;
    if (dev->addr == 0x19) {
        // SC7A20H 加速度计：识别码，以及"平放、屏幕朝上"的一组静态读数。
        // SC7A20H accelerometer: identity plus a static "lying flat, face up" sample.
        dev->regs[0x0F] = 0x11;
        dev->regs[0x70] = 0x28;
        dev->regs[0x27] = 0x0F;
        const int16_t z = (int16_t)(-1000 * 16);
        dev->regs[0x2C] = (uint8_t)(z & 0xFF);
        dev->regs[0x2D] = (uint8_t)((z >> 8) & 0xFF);
    }
    *out = dev;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) {
    free(dev);
    return ESP_OK;
}

// 加速度计用寄存器地址最高位表示"连续读写"，要去掉才是真实地址。
// The accelerometer flags auto-increment in the register MSB; strip it for the real address.
static uint8_t reg_addr(const struct sim_i2c_dev* dev, uint8_t raw) {
    return dev->addr == 0x19 ? (uint8_t)(raw & 0x7F) : raw;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t* data, size_t len, int timeout_ms) {
    (void)timeout_ms;
    if (len == 0) return ESP_OK;
    dev->ptr = reg_addr(dev, data[0]);
    for (size_t i = 1; i < len; i++) dev->regs[(uint8_t)(dev->ptr + i - 1)] = data[i];
    return ESP_OK;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t* data, size_t len, int timeout_ms) {
    (void)timeout_ms;
    for (size_t i = 0; i < len; i++) data[i] = dev->regs[(uint8_t)(dev->ptr + i)];
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(
    i2c_master_dev_handle_t dev, const uint8_t* wr, size_t wr_len, uint8_t* rd, size_t rd_len,
    int timeout_ms
) {
    if (wr_len > 0) dev->ptr = reg_addr(dev, wr[0]);
    return i2c_master_receive(dev, rd, rd_len, timeout_ms);
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms) {
    (void)bus; (void)address; (void)timeout_ms;
    return ESP_OK;
}

esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus) { (void)bus; return ESP_OK; }

/* ---- GPIO ---- */

static pthread_mutex_t s_input_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { int count; int x[2]; int y[2]; } s_touch;

esp_err_t gpio_config(const gpio_config_t* cfg) { (void)cfg; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level) { (void)gpio; (void)level; return ESP_OK; }
esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode) { (void)gpio; (void)mode; return ESP_OK; }
esp_err_t gpio_set_intr_type(gpio_num_t gpio, gpio_int_type_t type) { (void)gpio; (void)type; return ESP_OK; }
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t gpio, void (*fn)(void*), void* arg) {
    (void)gpio; (void)fn; (void)arg;
    return ESP_OK;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
esp_err_t gpio_wakeup_enable(gpio_num_t gpio, gpio_int_type_t type) { (void)gpio; (void)type; return ESP_OK; }
esp_err_t gpio_wakeup_disable(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
bool rtc_gpio_is_valid_gpio(gpio_num_t gpio) { (void)gpio; return true; }
esp_err_t rtc_gpio_pulldown_en(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
esp_err_t rtc_gpio_pullup_en(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
esp_err_t rtc_gpio_pullup_dis(gpio_num_t gpio) { (void)gpio; return ESP_OK; }

int gpio_get_level(gpio_num_t gpio) {
    // 触摸与 IO 扩展器的中断线低有效，空闲为高；加速度计 INT1 高有效，空闲为低。
    // Touch and IO-expander interrupts are active-low (idle high); accelerometer INT1 is active-high (idle low).
    if (gpio == READ_PICO_TP_INT_GPIO) {
        pthread_mutex_lock(&s_input_mu);
        const int touched = s_touch.count > 0;
        pthread_mutex_unlock(&s_input_mu);
        return touched ? 0 : 1;
    }
    if (gpio == READ_PICO_IOE_INT_GPIO) return 1;
    return 0;
}

/* ---- 墨水屏板定义 / EPD board definition ---- */

static bool s_rails_on;
static int s_vcom_mv = 1290;

static void board_init(uint32_t epd_row_width) { (void)epd_row_width; }
static void board_deinit(void) {}
static void board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    (void)state; (void)mask;
}
static void board_poweron(epd_ctrl_state_t* state) {
    (void)state;
    s_rails_on = true;
    sim_epd_rails(true);
}
static void board_poweroff(epd_ctrl_state_t* state) {
    (void)state;
    s_rails_on = false;
    sim_epd_rails(false);
}
static void board_measure_vcom(epd_ctrl_state_t* state) { (void)state; }
static float board_temperature(void) { return 25.0f; }
static void board_set_vcom(int mv) { s_vcom_mv = mv; }

const EpdBoardDefinition epd_board_read_pico = {
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .poweroff = board_poweroff,
    .measure_vcom = board_measure_vcom,
    .get_temperature = board_temperature,
    .set_vcom = board_set_vcom,
};

/* ---- 板级状态 / board status ---- */

sy7636a_handle_t read_pico_sy7636a(void) { return NULL; }
fca9555_handle_t read_pico_fca9555(void) { return NULL; }

static read_pico_i2c_census_t s_census;

void read_pico_i2c_census_take(void) {
    static const struct { uint8_t addr; const char* name; } map[READ_PICO_I2C_DEV_N] = {
        { 0x15, "CST836U" }, { 0x19, "SC7A20H" }, { 0x20, "FCA9555" }, { 0x2A, "CW32" }, { 0x62, "SY7636A" },
    };
    s_census = (read_pico_i2c_census_t){ .all_online = true };
    for (int i = 0; i < READ_PICO_I2C_DEV_N; i++) {
        s_census.dev[i].name = map[i].name;
        s_census.dev[i].addr = map[i].addr;
        s_census.dev[i].online = true;
    }
}

const read_pico_i2c_census_t* read_pico_i2c_census(void) { return &s_census; }

esp_err_t read_pico_get_ioe_status_full(read_pico_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    status->ioe_int_level = 1;
    status->rails_on = s_rails_on;
    return ESP_OK;
}
esp_err_t read_pico_get_ioe_status(read_pico_status_t* status) { return read_pico_get_ioe_status_full(status); }

esp_err_t read_pico_get_status(read_pico_status_t* status) {
    esp_err_t err = read_pico_get_ioe_status_full(status);
    if (err != ESP_OK) return err;
    status->sy_live = s_rails_on;
    status->sy.on = s_rails_on;
    status->sy.pg = s_rails_on;
    status->sy.vcom_manual = true;
    status->sy.vcom_mv = s_vcom_mv;
    status->sy.dly_ms[0] = status->sy.dly_ms[1] = status->sy.dly_ms[2] = status->sy.dly_ms[3] = 2;
    status->sy.temperature_c = 25;
    return ESP_OK;
}

int read_pico_ioe_int_level(void) { return 1; }
bool read_pico_rails_on(void) { return s_rails_on; }
esp_err_t read_pico_toggle_vcomctl(void) { return ESP_OK; }
esp_err_t read_pico_touch_reset(void) { return ESP_OK; }
esp_err_t read_pico_tp_rst(bool high) { (void)high; return ESP_OK; }
void read_pico_clear_ioe_int(void) {}
esp_err_t read_pico_ioe_sleep(bool host_off) { (void)host_off; return ESP_OK; }
esp_err_t read_pico_ioe_wake(void) { return ESP_OK; }

/* ---- 触摸 CST836U / touch ---- */

struct cst836u_dev_t {
    cst836u_mode_t mode;
};

void sim_touch_set(int count, int x0, int y0, int x1, int y1) {
    pthread_mutex_lock(&s_input_mu);
    s_touch.count = count;
    s_touch.x[0] = x0;
    s_touch.y[0] = y0;
    s_touch.x[1] = x1;
    s_touch.y[1] = y1;
    pthread_mutex_unlock(&s_input_mu);
}

esp_err_t cst836u_init(i2c_master_bus_handle_t bus, const cst836u_config_t* config, cst836u_handle_t* handle) {
    (void)bus; (void)config;
    static struct cst836u_dev_t dev;
    dev.mode = CST836U_MODE_NORMAL;
    *handle = &dev;
    return ESP_OK;
}
esp_err_t cst836u_deinit(cst836u_handle_t h) { (void)h; return ESP_OK; }

esp_err_t cst836u_read(cst836u_handle_t h, cst836u_touch_t* touch) {
    if (h == NULL || touch == NULL) return ESP_ERR_INVALID_ARG;
    memset(touch, 0, sizeof(*touch));
    if (h->mode == CST836U_MODE_DEEPSLEEP) return ESP_OK;
    pthread_mutex_lock(&s_input_mu);
    const int n = s_touch.count > CST836U_MAX_POINTS ? CST836U_MAX_POINTS : s_touch.count;
    for (int i = 0; i < n; i++) {
        touch->points[i].active = true;
        touch->points[i].id = (uint8_t)i;
        touch->points[i].event = 2;
        touch->points[i].x = (uint16_t)s_touch.x[i];
        touch->points[i].y = (uint16_t)s_touch.y[i];
    }
    pthread_mutex_unlock(&s_input_mu);
    touch->count = (uint8_t)n;
    touch->touched = n > 0;
    if (n > 0) {
        touch->x = touch->points[0].x;
        touch->y = touch->points[0].y;
    }
    return ESP_OK;
}

esp_err_t cst836u_get_info(cst836u_handle_t h, cst836u_info_t* info) {
    (void)h;
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    *info = (cst836u_info_t){ .fw = 1, .id = 0x36, .module = 1, .project = 1, .type = 0x836 };
    return ESP_OK;
}
esp_err_t cst836u_set_mode(cst836u_handle_t h, cst836u_mode_t mode) { h->mode = mode; return ESP_OK; }
cst836u_mode_t cst836u_get_mode(cst836u_handle_t h) { return h->mode; }
esp_err_t cst836u_reset(cst836u_handle_t h) { h->mode = CST836U_MODE_NORMAL; return ESP_OK; }
esp_err_t cst836u_wake(cst836u_handle_t h) { h->mode = CST836U_MODE_NORMAL; return ESP_OK; }
bool cst836u_int_asserted(cst836u_handle_t h) { (void)h; return gpio_get_level(READ_PICO_TP_INT_GPIO) == 0; }
int cst836u_int_level(cst836u_handle_t h) { (void)h; return gpio_get_level(READ_PICO_TP_INT_GPIO); }
gpio_num_t cst836u_int_gpio(cst836u_handle_t h) { (void)h; return READ_PICO_TP_INT_GPIO; }
const char* cst836u_mode_name(cst836u_mode_t mode) { return mode == CST836U_MODE_DEEPSLEEP ? "DEEPSLEEP" : "NORMAL"; }
const char* cst836u_event_name(uint8_t event) {
    static const char* names[] = { "DOWN", "UP", "CONTACT", "NONE" };
    return names[event & 3];
}

/* ---- 电源芯片 PMU：一节 78% 电量、未在充电的电池 / a battery at 78%, not charging ---- */

static pmu_snapshot_t s_pmu;
static bool s_key_short;
static bool s_key_wake;

void sim_power_key_press(void) {
    pthread_mutex_lock(&s_input_mu);
    if (sim_sleep_state() != 0) s_key_wake = true;
    else s_key_short = true;
    pthread_mutex_unlock(&s_input_mu);
}

bool sim_wake_key_pending(void) {
    pthread_mutex_lock(&s_input_mu);
    const bool pending = s_key_wake;
    pthread_mutex_unlock(&s_input_mu);
    return pending;
}

static void pmu_fill(void) {
    s_pmu.present = s_pmu.identity_ok = s_pmu.status_ok = s_pmu.diag_ok = true;
    s_pmu.event_ok = s_pmu.config_ok = s_pmu.time_ok = s_pmu.alarm_ok = true;
    s_pmu.proto_major = 1;
    s_pmu.fw_major = 1;
    s_pmu.hw_rev = 1;
    s_pmu.device_id = 0x5350;
    for (int i = 0; i < PMU_CHIP_UID_LEN; i++) s_pmu.uid[i] = (uint8_t)(0x51 + i);
    s_pmu.uid_ok = true;
    s_pmu.caps = PMU_CAP_BATTERY_ADC | PMU_CAP_CHARGE_STATUS | PMU_CAP_KEY_EVENTS;
    s_pmu.power_state = PMU_PWR_RUNNING;
    s_pmu.battery_mv = 3950;
    s_pmu.soc_permille = 780;
    s_pmu.mcu_temp_centi = 2500;
    s_pmu.charge_state = PMU_CHARGE_NOT_CHARGING;
    s_pmu.config.low_mv = 3500;
    s_pmu.config.critical_mv = 3300;
    s_pmu.config.full_mv = 4200;
    s_pmu.qb_mv = s_pmu.battery_mv;
    s_pmu.qb_soc = s_pmu.soc_permille;
    s_pmu.qb_charge = s_pmu.charge_state;
    s_pmu.time_synced = 1;
}

static void pmu_tick(void) {
    s_pmu.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_pmu.unix_sec = (uint32_t)time(NULL);
    s_pmu.last_err = ESP_OK;
}

esp_err_t read_pico_pmu_init(i2c_master_bus_handle_t bus, const read_pico_pmu_config_t* config) {
    (void)bus; (void)config;
    pmu_fill();
    pmu_tick();
    return ESP_OK;
}
esp_err_t read_pico_pmu_deinit(void) { return ESP_OK; }
bool read_pico_pmu_ready(void) { return s_pmu.present; }
const pmu_snapshot_t* read_pico_pmu_get(void) { return &s_pmu; }
esp_err_t read_pico_pmu_refresh(void) { pmu_tick(); return ESP_OK; }
esp_err_t read_pico_pmu_poll(void) { pmu_tick(); return ESP_OK; }

esp_err_t read_pico_pmu_cmd(uint16_t code, const uint8_t* payload, uint8_t plen) {
    (void)payload; (void)plen;
    s_pmu.last_op_code = code;
    s_pmu.last_op_status = PMU_STATUS_OK;
    return ESP_OK;
}

esp_err_t read_pico_pmu_vcom_get(int* out_mv) {
    if (out_mv == NULL) return ESP_ERR_INVALID_ARG;
    *out_mv = 1290;
    return ESP_OK;
}
esp_err_t read_pico_pmu_vcom_set(int mv) { (void)mv; return ESP_OK; }
esp_err_t read_pico_pmu_uid_get(uint8_t out[PMU_CHIP_UID_LEN]) {
    // 固件会传 NULL 只为触发一次读取。/ The firmware passes NULL just to trigger a read.
    if (out != NULL) memcpy(out, s_pmu.uid, PMU_CHIP_UID_LEN);
    return ESP_OK;
}
esp_err_t read_pico_pmu_event_ack(uint16_t event_id) { (void)event_id; return ESP_OK; }
esp_err_t read_pico_pmu_action(uint8_t action, uint16_t delay_ms, uint16_t reason) {
    (void)action; (void)delay_ms; (void)reason;
    return ESP_OK;
}

void read_pico_pmu_drain_events(void) {
    pthread_mutex_lock(&s_input_mu);
    s_key_short = false;
    pthread_mutex_unlock(&s_input_mu);
}

bool read_pico_pmu_take_key_short(void) {
    pthread_mutex_lock(&s_input_mu);
    const bool taken = s_key_short;
    s_key_short = false;
    pthread_mutex_unlock(&s_input_mu);
    return taken;
}

bool read_pico_pmu_take_key_wakeup(void) {
    pthread_mutex_lock(&s_input_mu);
    const bool taken = s_key_wake;
    s_key_wake = false;
    pthread_mutex_unlock(&s_input_mu);
    return taken;
}

esp_err_t read_pico_pmu_report_ready(void) { return ESP_OK; }

void sim_set_sleep_state(int state);  // sim_idf.c

// 真机的软睡和关机会切断主控供电，按电源键后从头开机。这里照做：停在原地等电源键，
// 然后整个模拟器重新启动，固件从 app_main 重新跑（屏幕上留着锁屏图，和真机一样）。
// On the device, soft sleep and power-off cut host power and the power key boots it afresh. Mirrored
// here: park until the power key, then restart the whole simulator so the firmware runs from
// app_main again (the lock image stays on screen meanwhile, as on hardware).
static esp_err_t host_power_down(void) {
    sim_set_sleep_state(2);
    while (!sim_quit_requested() && !sim_wake_key_pending()) usleep(10000);
    if (sim_quit_requested()) pthread_exit(NULL);
    esp_restart();
}
esp_err_t read_pico_pmu_report_sleep(void) { return host_power_down(); }
esp_err_t read_pico_pmu_power_off(void) { return host_power_down(); }

/* ---- TF 卡：sim/data/sdcard 目录 / TF card: the sim/data/sdcard directory ---- */

static volatile bool s_sd_present = true;
static volatile bool s_sd_ejected;  // 固件调过 read_pico_sd_sync()，卡被弹出 / the firmware called read_pico_sd_sync() and ejected the card

void sim_sd_set_present(bool present) {
    s_sd_present = present;
    s_sd_ejected = false;  // 重新插卡等于重新挂载 / re-inserting mounts it again
}
bool sim_sd_present(void) { return s_sd_present; }
bool sim_sd_mounted(void) { return s_sd_present && !s_sd_ejected; }
bool read_pico_sd_present(void) { return s_sd_present; }

esp_err_t read_pico_sd_start_probe(void) { return ESP_OK; }

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));
    info->present = s_sd_present;
    info->mounted = sim_sd_mounted();
    if (!info->mounted) {
        info->error = s_sd_present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
        return ESP_OK;
    }
    snprintf(info->name, sizeof(info->name), "SIM");
    return esp_vfs_fat_info("/sdcard", &info->capacity_bytes, &info->free_bytes);
}

esp_err_t read_pico_sd_remount(void) {
    if (!s_sd_present) return ESP_ERR_NOT_FOUND;
    s_sd_ejected = false;
    return ESP_OK;
}
// 格式化会清空目录；模拟器里不做，免得误删电脑上的文件。
// Formatting would wipe the directory; refused here so files on the computer are never deleted by accident.
esp_err_t read_pico_sd_format(void) {
    ESP_LOGW(TAG, "TF format is not simulated; sim/data/sdcard left untouched");
    return ESP_ERR_NOT_SUPPORTED;
}
// 名字叫 sync，真机上的实际作用是"安全弹出"：卸载卡，之后要重新挂载才能用。这里照做，
// 免得再出现"模拟器里没事、真机上掉卡"。
// Named sync, but on the device it safely ejects: the card is unmounted and must be remounted before
// use. Mirrored here so "fine in the simulator, card gone on hardware" cannot happen again.
esp_err_t read_pico_sd_sync(void) {
    s_sd_ejected = true;
    return ESP_OK;
}

/* ---- 蜂鸣器：只打日志 / buzzer: logs only ---- */

esp_err_t read_pico_buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    ESP_LOGI(TAG, "buzzer %u Hz for %u ms", (unsigned)frequency_hz, (unsigned)duration_ms);
    return ESP_OK;
}
esp_err_t read_pico_buzzer_score_play(void) { ESP_LOGI(TAG, "buzzer score play"); return ESP_OK; }
esp_err_t read_pico_buzzer_score_stop(void) { return ESP_OK; }
bool read_pico_buzzer_score_busy(void) { return false; }
esp_err_t read_pico_buzzer_probe(read_pico_buzzer_probe_t probe) {
    ESP_LOGI(TAG, "buzzer probe %d", (int)probe);
    return ESP_OK;
}
esp_err_t read_pico_buzzer_effect(void) { return ESP_OK; }

/* ---- Wi-Fi 传书：模拟器里未实现，把书直接放进 sim/data/sdcard/books 即可。
 * Wi-Fi transfer: not implemented; drop books into sim/data/sdcard/books instead. ---- */

esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t* cfg) { (void)cfg; return ESP_ERR_NOT_SUPPORTED; }
void read_pico_transfer_stop(void) {}
bool read_pico_transfer_try_stop_if_idle(void) { return true; }
void read_pico_transfer_get_status(read_pico_transfer_status_t* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->state = READ_PICO_TRANSFER_STOPPED;
    out->last_error = ESP_ERR_NOT_SUPPORTED;
}
void read_pico_transfer_service_poll(void) {}
esp_err_t read_pico_transfer_get_saved_wifi(char ssid[33], bool* configured) {
    if (ssid != NULL) ssid[0] = 0;
    if (configured != NULL) *configured = false;
    return ESP_OK;
}
esp_err_t read_pico_transfer_forget_wifi(void) { return ESP_OK; }
esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX], size_t* count) {
    (void)out;
    if (count != NULL) *count = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t read_pico_transfer_save_wifi(const char* ssid, const char* password) {
    (void)ssid; (void)password;
    return ESP_ERR_NOT_SUPPORTED;
}
