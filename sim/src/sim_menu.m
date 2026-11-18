/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * macOS 菜单栏里的"外观"菜单。菜单项被点中或快捷键按下时，往 SDL 的事件队列里
 * 投一个用户事件，由 sim_main.c 的主循环统一处理。
 *
 * The "外观" (Appearance) menu in the macOS menu bar. Choosing an item or
 * pressing its shortcut posts an SDL user event, handled in the sim_main.c loop.
 */
#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <string.h>

#include "sim_menu.h"

@interface SimMenuTarget : NSObject
- (void)choose:(NSMenuItem*)item;
@end

@implementation SimMenuTarget
- (void)choose:(NSMenuItem*)item {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_USEREVENT;
    ev.user.code = (Sint32)item.tag;
    SDL_PushEvent(&ev);
}
@end

static SimMenuTarget* s_target;

static void add_item(NSMenu* menu, NSString* title, NSString* key, int code) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(choose:) keyEquivalent:key];
    item.target = s_target;
    item.tag = code;
    [menu addItem:item];
}

void sim_menu_install(void) {
    // 无窗口运行（SDL_VIDEODRIVER=dummy）时没有菜单栏。/ No menu bar when running headless.
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == NULL || strcmp(driver, "cocoa") != 0 || NSApp == nil || [NSApp mainMenu] == nil) return;

    s_target = [[SimMenuTarget alloc] init];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"外观"];
    add_item(menu, @"随机配色", @"r", SIM_MENU_RANDOM_LOOK);
    add_item(menu, @"原机配色", @"0", SIM_MENU_STOCK_LOOK);

    NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:@"外观" action:nil keyEquivalent:@""];
    top.submenu = menu;
    [[NSApp mainMenu] addItem:top];

    NSMenu* card = [[NSMenu alloc] initWithTitle:@"TF 卡"];
    add_item(card, @"在访达中打开 TF 卡文件夹", @"o", SIM_MENU_OPEN_CARD);
    add_item(card, @"拔出 / 插入 TF 卡", @"e", SIM_MENU_TOGGLE_CARD);
    NSMenuItem* card_top = [[NSMenuItem alloc] initWithTitle:@"TF 卡" action:nil keyEquivalent:@""];
    card_top.submenu = card;
    [[NSApp mainMenu] addItem:card_top];
    fprintf(stderr, "[sim] menu bar: 外观, TF 卡 added, %ld top-level menus\n", (long)[[NSApp mainMenu] numberOfItems]);
}
