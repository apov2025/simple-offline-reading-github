/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏面板替身。epdiy 的绘图与高层差分（epdiy.c / highlevel.c）原样编译，
 * 这里只顶替它下面的渲染层：差分图生成、按波形把像素"刷"到一块内存面板上。
 *
 * EPD panel stand-in. epdiy drawing and high-level diffing are compiled as-is;
 * this file replaces the render layer below them: difference image and
 * "driving" pixels onto an in-memory panel according to the waveform.
 *
 * 面板模型 / Panel model:
 *   - 真实波形表决定某个 (from → to) 跳变有没有驱动动作；没有动作的像素保持原样。
 *     The real waveform table decides whether a (from → to) transition is driven; undriven pixels keep their value.
 *   - 灰阶模式（GC16/GL16）下，被驱动的像素直接落到目标灰度。
 *     In gray modes (GC16/GL16) a driven pixel lands on its target gray.
 *   - DU 只有黑白两个终点：净驱动偏黑落到 0，偏白落到 15。
 *     DU only ends at black or white: net-dark lands on 0, net-light on 15.
 *   - 耗时 = 相位数 × 帧周期，来自真实扫描时序。残影不模拟。
 *     Time = phases × frame period from the real scan timing. Ghosting is not simulated.
 */
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "e0470_epaper_waveform.h"
#include "epd_board.h"
#include "epd_lcd.h"
#include "epdiy.h"
#include "read_pico_epd_timing.h"
#include "sim.h"
#include "sim_idf.h"

static const char* TAG = "sim_epd";

static uint8_t s_panel[SIM_PANEL_H][SIM_PANEL_W];
static pthread_mutex_t s_panel_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_panel_gen = 1;
static sim_refresh_info_t s_info;
static volatile bool s_realtime = true;

/* ---- 给窗口线程 / for the window thread ---- */

uint32_t sim_panel_snapshot(uint8_t* out) {
    pthread_mutex_lock(&s_panel_mu);
    memcpy(out, s_panel, sizeof(s_panel));
    uint32_t gen = s_panel_gen;
    pthread_mutex_unlock(&s_panel_mu);
    return gen;
}

void sim_panel_info(sim_refresh_info_t* out) {
    pthread_mutex_lock(&s_panel_mu);
    *out = s_info;
    pthread_mutex_unlock(&s_panel_mu);
}

void sim_set_realtime(bool on) { s_realtime = on; }
bool sim_realtime(void) { return s_realtime; }

static void sim_wait_ms(int ms) {
    if (!s_realtime || ms <= 0) return;
    usleep((useconds_t)ms * 1000);
}

/* ---- 板定义的登记，与 epd_board.c 等价 / board registry, as in epd_board.c ---- */

static const EpdBoardDefinition* s_board;
static epd_ctrl_state_t s_ctrl_state;

const EpdBoardDefinition* epd_current_board() { return s_board; }
void epd_set_board(const EpdBoardDefinition* board) { s_board = board; }
epd_ctrl_state_t* epd_ctrl_state() { return &s_ctrl_state; }
void epd_set_mode(bool state) { (void)state; }
void epd_control_reg_init() {}
void epd_control_reg_deinit() {}
void epd_busy_delay(uint32_t cycles) { (void)cycles; }

void sim_epd_rails(bool on) {
    pthread_mutex_lock(&s_panel_mu);
    s_info.rails_on = on;
    pthread_mutex_unlock(&s_panel_mu);
}

/* ---- LCD 外设参数：只记下来 / LCD peripheral parameters: recorded only ---- */

static int s_prefill_lines;
static bool s_leading_skip;

void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height) {
    (void)config; (void)display_width; (void)display_height;
}
void epd_lcd_deinit(void) {}
void epd_lcd_set_pixel_clock_MHz(int frequency) { (void)frequency; }
void epd_lcd_set_line_timing(const LcdLineTiming_t* timing) { (void)timing; }
void epd_lcd_set_prefill_lines(int lines) { s_prefill_lines = lines; }
int epd_lcd_prefill_lines(void) { return s_prefill_lines; }
void epd_set_leading_skip(bool enable) { s_leading_skip = enable; }
int epd_last_leading_skip(void) { return 0; }
void epd_leading_skip_discard(void) {}

/* ---- 渲染层入口 / render layer entry points ---- */

void epd_renderer_init(enum EpdInitOptions options) {
    (void)options;
    pthread_mutex_lock(&s_panel_mu);
    // 断电的面板保留上一幅画面；模拟器每次启动从白屏开始。
    // A real panel keeps its last image when unpowered; the simulator starts white.
    memset(s_panel, 15, sizeof(s_panel));
    s_panel_gen++;
    pthread_mutex_unlock(&s_panel_mu);
}

void epd_renderer_deinit() {}

static void record(const char* mode, int phases, int est_ms, int pixels) {
    snprintf(s_info.mode, sizeof(s_info.mode), "%s", mode);
    s_info.phases = phases;
    s_info.est_ms = est_ms;
    s_info.pixels = pixels;
    s_info.count++;
    s_panel_gen++;
}

void epd_clear_area_cycles(EpdRect area, int cycles, int cycle_time) {
    (void)cycles; (void)cycle_time;
    int x1 = area.x + area.width, y1 = area.y + area.height;
    if (x1 > SIM_PANEL_W) x1 = SIM_PANEL_W;
    if (y1 > SIM_PANEL_H) y1 = SIM_PANEL_H;
    const int x0 = area.x < 0 ? 0 : area.x, y0 = area.y < 0 ? 0 : area.y;
    // 真机先压黑再擦白，各几轮；这里给一次黑闪再落白。
    // The device drives black then white for a few cycles; one black flash, then white.
    for (int pass = 0; pass < 2; pass++) {
        pthread_mutex_lock(&s_panel_mu);
        for (int y = y0; y < y1; y++) memset(&s_panel[y][x0], pass == 0 ? 0 : 15, (size_t)(x1 - x0));
        if (pass == 1) record("CLEAR", 0, 400, (x1 - x0) * (y1 - y0));
        else s_panel_gen++;
        pthread_mutex_unlock(&s_panel_mu);
        sim_wait_ms(200);
    }
}

void epd_clear_area(EpdRect area) { epd_clear_area_cycles(area, 3, 12); }

void epd_difference_column_range(EpdRect crop_to, int* x_start, int* x_end) {
    const int fb_width = epd_width();
    int start = crop_to.x & ~31;
    int end = (crop_to.x + crop_to.width + 31) & ~31;
    if (start < 0) start = 0;
    if (end > fb_width) end = fb_width;
    if (end < start) end = start;
    *x_start = start;
    *x_end = end;
}

// 与 render.c 的 epd_difference_image_base 同语义：差分图每像素一字节 (to << 4) | from，
// 只算裁剪矩形覆盖的列段（外扩到 32 像素），行/列脏标记照填。
// Same contract as render.c: one byte per pixel, (to << 4) | from, computed only
// for the cropped column span (widened to 32 px), with dirty line/column marks.
EpdRect epd_difference_image_cropped(
    const uint8_t* to, const uint8_t* from, EpdRect crop_to, uint8_t* interlaced, bool* dirty_lines,
    uint8_t* col_dirtiness
) {
    const int fb_width = epd_width();
    const int fb_height = epd_height();
    memset(col_dirtiness, 0, (size_t)fb_width / 2);
    memset(dirty_lines, 0, sizeof(bool) * (size_t)fb_height);

    int x_end = crop_to.x + crop_to.width;
    int y_end = crop_to.y + crop_to.height;
    if (x_end > fb_width) x_end = fb_width;
    if (y_end > fb_height) y_end = fb_height;
    int x_start, x_stop;
    epd_difference_column_range(crop_to, &x_start, &x_stop);

    for (int y = crop_to.y < 0 ? 0 : crop_to.y; y < y_end; y++) {
        const uint8_t* to_row = to + (size_t)y * fb_width / 2;
        const uint8_t* from_row = from + (size_t)y * fb_width / 2;
        uint8_t* out_row = interlaced + (size_t)y * fb_width;
        uint8_t dirty = 0;
        for (int x = x_start; x < x_stop; x++) {
            const uint8_t t = (x & 1) ? (to_row[x / 2] >> 4) : (to_row[x / 2] & 0x0F);
            const uint8_t f = (x & 1) ? (from_row[x / 2] >> 4) : (from_row[x / 2] & 0x0F);
            col_dirtiness[x / 2] |= (uint8_t)((t ^ f) << (4 * (x & 1)));
            dirty |= (uint8_t)(t ^ f);
            out_row[x] = (uint8_t)((t << 4) | f);
        }
        dirty_lines[y] = dirty != 0;
    }

    int min_x, min_y, max_x, max_y;
    for (min_x = crop_to.x; min_x < x_end; min_x++) {
        if ((col_dirtiness[min_x / 2] & ((min_x & 1) ? 0xF0 : 0x0F)) != 0) break;
    }
    for (max_x = x_end - 1; max_x >= crop_to.x; max_x--) {
        if ((col_dirtiness[max_x / 2] & ((max_x & 1) ? 0xF0 : 0x0F)) != 0) break;
    }
    for (min_y = crop_to.y; min_y < y_end; min_y++) {
        if (dirty_lines[min_y]) break;
    }
    for (max_y = y_end - 1; max_y >= crop_to.y; max_y--) {
        if (dirty_lines[max_y]) break;
    }
    EpdRect crop_rect = {
        .x = min_x,
        .y = min_y,
        .width = max_x >= min_x ? max_x - min_x + 1 : 0,
        .height = max_y >= min_y ? max_y - min_y + 1 : 0,
    };
    return crop_rect;
}

EpdRect epd_difference_image(
    const uint8_t* to, const uint8_t* from, uint8_t* interlaced, bool* dirty_lines, uint8_t* col_dirtiness
) {
    return epd_difference_image_cropped(to, from, epd_full_screen(), interlaced, dirty_lines, col_dirtiness);
}

static const char* mode_name(const EpdWaveform* waveform, int type) {
    if (waveform == &E0470_FOLLOW_WAVEFORM) return "FOLLOW DU";
    switch (type) {
        case MODE_DU: return "DU";
        case MODE_GC16: return "GC16";
        case MODE_GL16: return "GL16";
        default: return "OTHER";
    }
}

// 汇总一张波形表里每个 (to << 4 | from) 的净驱动：正数偏黑，负数偏白；active 表示有没有动作。
// Per (to << 4 | from): net drive across all phases (positive = darker), and whether any phase acts.
static void waveform_summary(const EpdWaveformPhases* phases, int8_t net[256], bool active[256]) {
    memset(net, 0, 256);
    memset(active, 0, 256 * sizeof(bool));
    for (int frame = 0; frame < phases->phases; frame++) {
        const uint8_t* lut = phases->luts + 64 * frame;
        for (int to = 0; to < 16; to++) {
            for (int from = 0; from < 16; from++) {
                const uint8_t op = (lut[to * 4 + from / 4] >> (6 - 2 * (from % 4))) & 3;
                const int idx = (to << 4) | from;
                if (op == 1) { net[idx]++; active[idx] = true; }
                else if (op == 2) { net[idx]--; active[idx] = true; }
            }
        }
    }
}

enum EpdDrawError epd_draw_base(
    EpdRect area, const uint8_t* data, EpdRect crop_to, enum EpdDrawMode mode, int temperature,
    const bool* drawn_lines, const uint8_t* drawn_columns, const EpdWaveform* waveform
) {
    (void)area; (void)crop_to;
    if (waveform == NULL) return EPD_DRAW_NO_PHASES_AVAILABLE;
    if (!(mode & MODE_PACKING_1PPB_DIFFERENCE)) {
        ESP_LOGW(TAG, "epd_draw_base: packing 0x%x not simulated", (unsigned)mode);
        return EPD_DRAW_SUCCESS;
    }

    const int type = mode & 0x3F;
    const EpdWaveformMode* wmode = NULL;
    for (int i = 0; i < waveform->num_modes; i++) {
        if (waveform->mode_data[i]->type == type) wmode = waveform->mode_data[i];
    }
    if (wmode == NULL) return EPD_DRAW_MODE_NOT_FOUND;
    int range = 0;
    for (int i = 0; i < waveform->num_temp_ranges; i++) {
        if (temperature >= waveform->temp_intervals[i].min) range = i;
    }
    if (range >= wmode->temp_ranges) range = wmode->temp_ranges - 1;
    const EpdWaveformPhases* phases = wmode->range_data[range];

    int8_t net[256];
    bool active[256];
    waveform_summary(phases, net, active);

    const bool is_du = type == MODE_DU;
    const int est_ms = read_pico_epd_refresh_ms(
        read_pico_epd_pclk_mhz(),
        waveform == &E0470_WAVEFORM || waveform == &E0470_FULL_WAVEFORM || waveform == &E0470_GRAY8_WAVEFORM
            ? READ_PICO_EPD_SCAN_FULL : READ_PICO_EPD_SCAN_FAST,
        phases->phases
    );

    // 灰阶全刷会先整体压黑再出图，中途给一帧反相，看得出"闪"了一下。
    // A gray full update drives through black first; show one inverted frame mid-way so the flash is visible.
    const bool flash = type == MODE_GC16 && drawn_lines != NULL;
    int driven = 0;
    for (int pass = flash ? 0 : 1; pass < 2; pass++) {
        driven = 0;
        pthread_mutex_lock(&s_panel_mu);
        for (int y = 0; y < SIM_PANEL_H; y++) {
            if (drawn_lines != NULL && !drawn_lines[y]) continue;
            const uint8_t* row = data + (size_t)y * SIM_PANEL_W;
            for (int x = 0; x < SIM_PANEL_W; x++) {
                if (drawn_columns != NULL && (drawn_columns[x / 2] & ((x & 1) ? 0xF0 : 0x0F)) == 0) continue;
                const uint8_t idx = row[x];
                if (!active[idx]) continue;
                driven++;
                uint8_t value = idx >> 4;
                if (is_du) {
                    if (net[idx] == 0) continue;
                    value = net[idx] > 0 ? 0 : 15;
                }
                s_panel[y][x] = pass == 0 ? (uint8_t)(15 - s_panel[y][x]) : value;
            }
        }
        if (pass == 1) record(mode_name(waveform, type), phases->phases, est_ms, driven);
        else s_panel_gen++;
        pthread_mutex_unlock(&s_panel_mu);
        sim_wait_ms(flash ? est_ms / 2 : est_ms);
    }
    ESP_LOGD(TAG, "%s: %d phases, ~%d ms, %d px", mode_name(waveform, type), phases->phases, est_ms, driven);
    return EPD_DRAW_SUCCESS;
}
