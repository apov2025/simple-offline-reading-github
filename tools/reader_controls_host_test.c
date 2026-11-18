// 验证按键去抖和前光安全范围。/ Verify key debouncing and safe frontlight bounds.
#include <assert.h>
#include <stdio.h>
#include "reader_controls.h"

int main(void) {
    assert(reader_controls_front_key(true, 0) == READER_KEY_HOME);
    assert(reader_controls_front_key(true, 1) == READER_KEY_SETTINGS);
    assert(reader_controls_front_key(true, 2) == READER_KEY_REFRESH);
    assert(reader_controls_front_key(true, 3) == READER_KEY_NONE);
    assert(reader_controls_front_key(false, 0) == READER_KEY_SETTINGS);
    assert(reader_controls_front_key(false, 1) == READER_KEY_BACK);
    reader_key_state_t s = {0};
    assert(reader_controls_side_update(&s, true, 1, 0) == 0);
    assert(reader_controls_side_update(&s, true, 1, 100) == 0);
    assert(reader_controls_side_update(&s, true, 0, 110) == 0);
    assert(reader_controls_side_update(&s, true, 0, 140) == 0);
    assert(reader_controls_side_update(&s, true, 1, 150) == 0);
    assert(reader_controls_side_update(&s, true, 0, 160) == 0);
    assert(reader_controls_side_update(&s, true, 1, 170) == 0);
    assert(reader_controls_side_update(&s, true, 1, 199) == 0);
    assert(reader_controls_side_update(&s, true, 1, 200) == 1);
    assert(reader_controls_side_update(&s, true, 1, 700) == 0);
    assert(reader_controls_side_update(&s, false, 0, 800) == 0);
    assert(reader_controls_side_update(&s, true, 1, 900) == 0);
    assert(reader_controls_side_update(&s, true, 0, 1000) == 0);
    assert(reader_controls_side_update(&s, true, 0, 1030) == 0);
    assert(reader_controls_side_update(&s, true, 6, 1040) == 0);
    assert(reader_controls_side_update(&s, false, 6, 1070) == 0);
    assert(reader_controls_side_update(&s, true, 6, 1080) == 0);
    assert(reader_controls_side_update(&s, true, 6, 1110) == 6);
    assert(reader_controls_side_update(&s, true, 6, 900) == 0);
    uint16_t x,y;
    assert(reader_controls_touch_xy(0,0,1,&x,&y) && x==0 && y==0);
    assert(reader_controls_touch_xy(683,1215,1,&x,&y) && x==683 && y==1215);
    assert(reader_controls_touch_xy(100,1600,1,&x,&y) && x==80 && y==1500);
    assert(reader_controls_touch_xy(200,1600,1,&x,&y) && x==240 && y==1500);
    assert(reader_controls_touch_xy(300,1600,1,&x,&y) && x==400 && y==1500);
    assert(!reader_controls_touch_xy(200,1600,2,&x,&y));
    assert(!reader_controls_touch_xy(200,1350,1,&x,&y));
    assert(!reader_controls_touch_xy(700,100,1,&x,&y));
    assert(!reader_controls_touch_xy(0,0,0,&x,&y));
    uint16_t cool, warm;
    reader_controls_light(3, 0, false, &cool, &warm);
    assert(cool == 1000 && warm == 0);
    reader_controls_light(2, 1, false, &cool, &warm);
    assert(cool == 250 && warm == 250);
    reader_controls_light(1, 2, false, &cool, &warm);
    assert(cool == 0 && warm == 200);
    reader_controls_light(3, 1, true, &cool, &warm);
    assert(cool == 0 && warm == 0);
    reader_controls_light(255, 255, false, &cool, &warm);
    assert(cool == 0 && warm == 0);
    for (int l=0;l<256;l++) for(int w=0;w<256;w++) {
        reader_controls_light(l,w,false,&cool,&warm);
        assert(cool + warm <= 1000);
    }
    puts("reader controls: PASS");
}
