/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 系统菜单栏里的模拟器菜单（"外观"和"TF 卡"）。菜单项以 SDL_USEREVENT 的形式送达，code 是下面的值。
 * Simulator menu in the system menu bar. Items arrive as SDL_USEREVENT with one of these codes.
 */
#pragma once

enum {
    SIM_MENU_RANDOM_LOOK = 1,  ///< 随机配色（⌘R）/ Random colorway
    SIM_MENU_STOCK_LOOK = 2,   ///< 原机配色（⌘0）/ Stock colorway
    SIM_MENU_OPEN_CARD = 3,    ///< 在访达中打开 TF 卡文件夹（⌘O）/ Reveal the TF card folder in Finder
    SIM_MENU_TOGGLE_CARD = 4,  ///< 拔出或插入 TF 卡（⌘E）/ Eject or insert the TF card
};

/// 在菜单栏里加上"外观"菜单；窗口创建之后调用。没有菜单栏的平台上什么也不做。
/// Add the Appearance menu to the menu bar; call after the window exists. A no-op where there is no menu bar.
void sim_menu_install(void);
