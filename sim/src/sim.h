/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 模拟器内部共享状态：窗口线程（SDL，主线程）与固件线程之间的全部交互都经这里。
 *
 * Shared simulator state. Everything that crosses between the window thread
 * (SDL, main thread) and the firmware thread goes through here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SIM_PANEL_W 1216  ///< 面板原生宽（横向）/ Native panel width (landscape)
#define SIM_PANEL_H 684   ///< 面板原生高 / Native panel height
#define SIM_VIEW_W 684    ///< 手持方向的显示宽 / Display width as held
#define SIM_VIEW_H 1216   ///< 手持方向的显示高 / Display height as held
#define SIM_KEY_STRIP_H 120  ///< 屏幕下方三键区在窗口里的高度 / Key strip height in the window
#define SIM_KEY_TOUCH_Y 1500 ///< 三键在触摸坐标系里的 y / Key y in touch coordinates

/* ---- 数据目录 / data directory ---- */
/// sim/data 的绝对路径；其下 sdcard/ flash/ nvs/。/ Absolute sim/data; holds sdcard/ flash/ nvs/.
const char* sim_data_dir(void);
/// 把固件路径（/sdcard/...、/flash/...）映射成主机路径；其余原样返回。
/// Map a firmware path to a host path; other paths are returned unchanged.
const char* sim_map_path(const char* path, char* buf, int buf_len);

/* ---- 面板 / panel ---- */
/// 取面板当前画面（原生朝向，每像素 0..15）。gen 每次变化加一。
/// Copy the current panel image (native orientation, 0..15 per pixel). gen bumps on change.
uint32_t sim_panel_snapshot(uint8_t* out);
typedef struct {
    char mode[16];      ///< 最近一次刷新的模式名 / Last update mode
    int phases;         ///< 波形相位数 / Waveform phases
    int est_ms;         ///< 按相位数与帧周期估算的耗时 / Estimated from phases × frame period
    int pixels;         ///< 实际被驱动的像素数 / Pixels actually driven
    int count;          ///< 开机以来的刷新次数 / Updates since boot
    bool rails_on;      ///< 屏幕高压轨是否上电 / EPD high-voltage rails on
} sim_refresh_info_t;
void sim_panel_info(sim_refresh_info_t* out);
/// 刷新是否按估算耗时停顿；关掉后瞬间完成。/ Whether updates take their estimated time.
void sim_set_realtime(bool on);
bool sim_realtime(void);

/* ---- 输入 / input ---- */
/// 窗口线程写入触摸状态；坐标是显示逻辑坐标，y 可以落在 1300 以下的三键区。
/// Window thread posts touch state in display coordinates; y may land in the key strip.
void sim_touch_set(int count, int x0, int y0, int x1, int y1);
/// 电源键短按一次。/ One short press of the power key.
void sim_power_key_press(void);
/// TF 卡插拔。/ Insert or remove the TF card.
void sim_sd_set_present(bool present);
bool sim_sd_present(void);
/// 卡插着并且已挂载（固件"安全弹出"之后卡还插着，但文件不可访问）。
/// Card inserted and mounted (after the firmware ejects it the card is still in, but files are out of reach).
bool sim_sd_mounted(void);

/* ---- 生命周期 / lifecycle ---- */
void sim_set_argv(char** argv);
/// 窗口关闭时置位；固件线程里的阻塞等待都会查它。/ Set on window close; blocking waits poll it.
void sim_request_quit(void);
bool sim_quit_requested(void);
/// 固件处于浅睡或关机，等待电源键。/ Firmware is asleep or off, waiting for the power key.
int sim_sleep_state(void);  ///< 0 运行 / running, 1 浅睡 / light sleep, 2 关机 / off
