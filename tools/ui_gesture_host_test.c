/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实识别器的时序与边界测试。
 * English: Timing and boundary tests for the actual recognizer.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "ui_gesture.h"
#include "ui_kit.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static ui_gesture_t g;
static ui_gesture_event_t event;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static bool feed(int x, int y, int64_t time, bool down, bool press, bool release, bool consumed) {
    touch = (cst836u_touch_t){ .x = x, .y = y, .touched = down, .count = down ? 1 : 0 };
    ctx = (app_ctx_t){ .touch = &touch, .now_ms = time, .pressed = press, .released = release, .consumed = consumed };
    return ui_gesture_feed(&g, &ctx, &event);
}
static void press(int x, int y, int64_t time) {
    ui_gesture_reset(&g);
    assert(feed(x, y, time, true, true, false, false));
    assert(event.type == UI_GESTURE_PRESS && event.hold_ms == 0);
}
static void long_drag_tests(void) {
    // 框选后纵移仍有效，横移报告当前位置；静止不重复派发。/ After framing, vertical drift remains valid; horizontal changes report current coordinates only once.
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_PRESS);
    ui_gesture_allow_long_drag(&g, true);
    assert(feed(100, 300, 501, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_DRAG && event.x == 100 && event.y == 300);
    assert(event.x0 == 100 && event.y0 == 100);
    assert(!feed(100, 300, 502, true, false, false, false));
    assert(feed(260, 300, 503, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_DRAG && event.x == 260 && event.y == 300);
    assert(feed(260, 300, 504, false, false, true, false));
    assert(event.type == UI_GESTURE_LONG_RELEASE && event.x == 260 && event.y == 300);
    assert(!feed(260, 300, 505, false, false, true, false));

    // 有效松手也清除授权；下一次长按不继承拖动资格。/ A clean release also clears permission for the next hold.
    assert(feed(100, 100, 700, true, true, false, false));
    assert(feed(100, 100, 1200, true, false, false, false));
    assert(!feed(300, 100, 1201, true, false, false, false));
    assert(feed(300, 100, 1202, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);

    // 有效长按后的松手可落在容差外，不必先收到移动快照。/ A clean held release may land beyond slop without an intermediate movement sample.
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    ui_gesture_allow_long_drag(&g, true);
    assert(feed(300, 400, 501, false, false, true, false));
    assert(event.type == UI_GESTURE_LONG_RELEASE && event.x == 300 && event.y == 400);

    // 达阈值前不可授权；越界后移回不能补成长按。/ Permission before threshold is ignored; returning after early drift cannot create a hold.
    press(100, 100, 0);
    ui_gesture_allow_long_drag(&g, true);
    assert(!feed(125, 100, 499, true, false, false, false));
    assert(!feed(100, 100, 500, true, false, false, false));
    ui_gesture_allow_long_drag(&g, true);
    assert(feed(100, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    ui_gesture_allow_long_drag(&g, true);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(!feed(100, 300, 501, true, false, false, false));
    ui_gesture_allow_long_drag(&g, true);
    assert(feed(100, 300, 502, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);

    // 授权后的各种中断清除策略，下一次未授权长按仍按旧规则取消。/ Every interruption clears permission; a later unapproved hold retains normal cancellation.
    for (int kind = 0; kind < 6; ++kind) {
        press(100, 100, 0);
        assert(feed(100, 100, 500, true, false, false, false));
        ui_gesture_allow_long_drag(&g, true);
        if (kind == 0) touch.count = 2;
        if (kind == 1) ctx.touch = NULL;
        if (kind == 2) ctx.consumed = true;
        if (kind == 3) ctx.now_ms = 499;
        if (kind < 4) {
            assert(ui_gesture_feed(&g, &ctx, &event));
            assert(event.type == UI_GESTURE_CANCEL);
        } else if (kind == 4) {
            ui_gesture_reset(&g);
        } else {
            assert(feed(100, 100, 501, false, false, false, false));
            assert(event.type == UI_GESTURE_CANCEL);
        }
        assert(!feed(100, 100, 600, false, false, true, false));
        assert(feed(100, 100, 700, true, true, false, false));
        assert(feed(100, 100, 1200, true, false, false, false));
        assert(!feed(300, 100, 1201, true, false, false, false));
        assert(feed(300, 100, 1202, false, false, true, false));
        assert(event.type == UI_GESTURE_CANCEL);
    }
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    ui_gesture_allow_long_drag(&g, true);
    ui_gesture_allow_long_drag(&g, false);
    assert(!feed(100, 300, 501, true, false, false, false));
    assert(feed(100, 300, 502, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    ui_gesture_allow_long_drag(NULL, true);
}
int main(void) {
    long_drag_tests();
    press(200, 300, 100);
    assert(!feed(200, 300, 100, true, true, false, false));
    assert(feed(210, 310, 200, false, false, true, false));
    assert(event.type == UI_GESTURE_TAP && event.x0 == 200 && event.y0 == 300);
    assert(event.x == 210 && event.y == 310 && event.hold_ms == 100);
    assert(!feed(210, 310, 200, false, false, true, false));
    press(100, 100, 0);
    assert(!feed(124, 100, 499, true, false, false, false));
    assert(feed(124, 100, 500, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_PRESS);
    assert(!feed(124, 100, 900, true, false, false, false));
    assert(feed(124, 100, 901, false, false, true, false));
    assert(event.type == UI_GESTURE_LONG_RELEASE && event.hold_ms == 901);
    assert(event.x0 == 100 && event.x == 124);
    assert(!feed(124, 100, 902, false, false, true, false));
    press(100, 100, 0);
    assert(feed(100, 100, 499, false, false, true, false));
    assert(event.type == UI_GESTURE_TAP);
    press(100, 100, 0);
    assert(feed(100, 100, 500, false, false, true, false));
    assert(event.type == UI_GESTURE_LONG_RELEASE && event.hold_ms == 500);
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_PRESS);
    assert(!feed(125, 100, 501, true, false, false, false));
    assert(feed(100, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(feed(125, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(feed(100, 100, 600, false, false, false, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    assert(feed(100, 100, 600, false, false, true, true));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    touch.count = 2;
    assert(ui_gesture_feed(&g, &ctx, &event) && event.type == UI_GESTURE_CANCEL);
    assert(!feed(100, 100, 600, false, false, true, false));
    press(100, 100, 0);
    assert(feed(100, 100, 500, true, false, false, false));
    ui_gesture_reset(&g);
    assert(!feed(100, 100, 600, false, false, true, false));
    const int dx[] = {-64, 64, 0, 0}, dy[] = {0, 0, -64, 64};
    const ui_gesture_type_t kinds[] = {UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U, UI_GESTURE_SWIPE_D};
    for (int i = 0; i < 4; ++i) {
        press(200, 300, 100);
        assert(!feed(200 + dx[i], 300 + dy[i], 150, true, false, false, false));
        assert(feed(200 + dx[i], 300 + dy[i], 200, false, false, true, false));
        assert(event.type == kinds[i]);
    }
    press(100, 100, 0);
    assert(!feed(125, 100, 500, true, false, false, false));
    assert(feed(100, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(220, 220, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(163, 100, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 163, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(200, 300, 0);
    assert(!feed(136, 300, 500, true, false, false, false));
    assert(feed(136, 300, 501, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(100, 100, 0);
    assert(!feed(117, 117, 500, true, false, false, false));
    assert(feed(117, 117, 600, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 10, true, false, false, true)); assert(event.type == UI_GESTURE_CANCEL);
    assert(!feed(100, 100, 20, false, false, true, false));
    press(0, 0, INT64_MAX - 500);
    assert(feed(0, 0, INT64_MAX, true, false, false, false)); assert(event.type == UI_GESTURE_LONG_PRESS);
    press(65535, 65535, 100);
    assert(feed(0, 65535, 200, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(10, 10, 100);
    assert(feed(10, 10, 99, true, false, false, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); ctx.touch = NULL;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); touch.count = 2; ctx.pressed = false;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    ui_gesture_reset(&g);
    assert(!feed(10, 10, 100, true, true, false, true));
    assert(feed(10, 10, 100, true, true, false, false));
    assert(feed(10, 10, INT64_MIN, true, false, false, false));
    assert(event.type == UI_GESTURE_CANCEL && event.hold_ms == 0);
    assert(!ui_gesture_feed(NULL, &ctx, &event));
    puts("gesture host tests passed");
}
