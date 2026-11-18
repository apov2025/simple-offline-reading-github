/*
 * SPDX-License-Identifier: Apache-2.0
 * 阅读器控制策略，不访问硬件。/ Reader control policy without hardware access.
 */
#include "reader_controls.h"

reader_key_action_t reader_controls_front_key(bool metalio, int position) {
    if (position < 0 || position > 2) return READER_KEY_NONE;
    if (position == 2) return READER_KEY_REFRESH;
    if (metalio) return position == 0 ? READER_KEY_HOME : READER_KEY_SETTINGS;
    return position == 0 ? READER_KEY_SETTINGS : READER_KEY_BACK;
}

uint8_t reader_controls_side_update(reader_key_state_t* s, bool valid, uint8_t pressed, int64_t now) {
    if (!valid) {
        s->interrupted = true;
        return 0;
    }
    pressed &= 15;
    if (!s->initialized || now < s->last_ms) {
        *s = (reader_key_state_t){.initialized=true, .stable=pressed, .candidate=pressed,
            .armed=(uint8_t)(~pressed & 15), .since_ms=now, .last_ms=now};
        return 0;
    }
    s->last_ms = now;
    if (pressed != s->candidate || s->interrupted) {
        s->candidate = pressed;
        s->since_ms = now;
        s->interrupted = false;
        return 0;
    }
    if (now - s->since_ms < 30 || pressed == s->stable) return 0;
    const uint8_t events = pressed & (uint8_t)~s->stable & s->armed;
    s->stable = pressed;
    s->armed = (s->armed | (uint8_t)~pressed) & (uint8_t)~events & 15;
    return events;
}

void reader_controls_light(uint8_t level, uint8_t warmth, bool asleep, uint16_t* cool, uint16_t* warm) {
    static const uint16_t duty[] = {0, 200, 500, 1000};
    *cool = *warm = 0;
    if (asleep || level > 3 || warmth > 2) return;
    *warm = (uint16_t)(duty[level] * warmth / 2);
    *cool = duty[level] - *warm;
}

bool reader_controls_touch_xy(uint16_t rx,uint16_t ry,uint8_t count,uint16_t* x,uint16_t* y) {
    if(!count || count>2)return false;
    if(ry>=1570 && ry<=1630 && count==1) {
        int pos=rx>=80 && rx<=120?0:rx>=180 && rx<=220?1:rx>=280 && rx<=320?2:-1;
        if(pos<0)return false;
        *x=80+160*pos;*y=1500;return true;
    }
    if(rx>=684 || ry>=1216)return false;
    *x=rx;*y=ry;return true;
}
