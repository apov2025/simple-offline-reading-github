/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 真机外壳：用小纸 Pico 的 3D 模型渲染，可换配色，并把鼠标点击换算成
 * "按了屏幕哪里 / 哪个键"。模拟器窗口固定用正视；视角接口留着，只为渲染器
 * 自己和将来需要别的角度时用。
 *
 * Device shell rendered from the Read Pico 3D model: recolorable, and able to
 * map a mouse position back to the screen or a key. The simulator window stays
 * head-on; the view API remains for the renderer itself and for any future
 * need of another angle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// 窗口画布的逻辑尺寸。正视时屏幕在画布里正好是 684 px 宽。
/// Logical canvas size. Seen head-on, the screen is exactly 684 px wide on it.
#define SHELL_W 896
#define SHELL_H 1506

typedef enum {
    SHELL_HIT_NONE = 0,
    SHELL_HIT_SCREEN,
    SHELL_HIT_KEY1,
    SHELL_HIT_KEY2,
    SHELL_HIT_KEY3,
    SHELL_HIT_POWER,
} sim_shell_hit_t;

void sim_shell_init(void);

/// 视角：yaw 绕竖轴、pitch 绕横轴，弧度；(0, 0) 是正视。
/// View angles in radians: yaw about the vertical axis, pitch about the horizontal; (0, 0) is head-on.
void sim_shell_set_view(float yaw, float pitch);
void sim_shell_get_view(float* yaw, float* pitch);

/// 配色。seed 为 0 时恢复原机配色（铝中框 + 白色玻璃），否则按 seed 生成一套协调配色。
/// Colorway. seed 0 restores the stock look (aluminum frame, white glass); otherwise a harmonious look from seed.
void sim_shell_set_look(unsigned seed);

/// 把外壳渲染进 out（ARGB8888，out_w × out_h，画布等比铺满）。quality 是每边的超采样倍数。
/// 屏幕窗口处是透明的（形状取自模型，含圆角），要先画固件画面、再把外壳盖上去。
/// Render the shell into out (ARGB8888, out_w × out_h, canvas scaled to fit). quality is the supersampling factor per axis.
/// The screen window is transparent (shape from the model, rounded corners included): draw the firmware image first, the shell on top.
void sim_shell_render(uint32_t* out, int out_w, int out_h, int quality);

/// 屏幕四角在画布上的位置（左上、右上、右下、左下）和明暗系数；屏幕背对观察者时返回 false。
/// Canvas positions of the screen corners (TL, TR, BR, BL) and a shading factor; false when the screen faces away.
bool sim_shell_screen_quad(float xy[4][2], float* shade);

/// 屏幕上一点（u、v 各 0..1，左上为原点）在画布上的位置。透视下屏幕不是平行四边形，
/// 贴图要分成小格逐格取点。
/// Canvas position of a screen point (u, v in 0..1 from the top left). Under perspective the
/// screen is not a parallelogram, so texture mapping samples this on a grid.
void sim_shell_screen_to_canvas(float u, float v, float* x, float* y);

/// 画布上一点对应的屏幕坐标（可能超出 0..683 / 0..1215）；屏幕背对观察者时返回 false。
/// Screen coordinates under a canvas point (may fall outside the screen); false when the screen faces away.
bool sim_shell_to_screen(float x, float y, float* sx, float* sy);

/// 画布上一点落在外壳的哪个部位。/ Which part of the shell a canvas point lands on.
sim_shell_hit_t sim_shell_hit(float x, float y);

/// 第 key 个触摸键（0..2）和电源键在画布上的位置，用来画按下反馈。
/// Canvas position of touch key 0..2 and of the power key, for press feedback.
void sim_shell_key_canvas(int key, float* x, float* y);
void sim_shell_power_canvas(float* x, float* y);
