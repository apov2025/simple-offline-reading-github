/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_battery.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    ui_battery_t b={0};
    assert(!ui_battery_low(&b));
    ui_battery_update(&b,true,800,false);
    assert(b.valid && b.percent==80 && !ui_battery_low(&b));
    ui_battery_update(&b,true,200,false);
    assert(ui_battery_low(&b));
    ui_battery_update(&b,true,240,true);
    assert(ui_battery_low(&b) && b.charging);
    ui_battery_update(&b,true,250,true);
    assert(!ui_battery_low(&b));
    ui_battery_update(&b,true,0,false);
    assert(b.valid && b.percent==0 && ui_battery_low(&b));
    ui_battery_update(&b,false,0,false);
    assert(!b.valid && !ui_battery_low(&b));
    ui_battery_update(&b,true,1001,false);
    assert(!b.valid);
    ui_battery_update(&b,true,1000,false);
    assert(b.valid && b.percent==100 && !ui_battery_low(&b));
    puts("battery: validity, zero/full, low threshold and recovery hysteresis PASS");
}
