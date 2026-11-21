/* SPDX-License-Identifier: Apache-2.0
 * 使用主循环已更新的快照，不增加I2C流量、不确认按键事件。/ Use the loop's snapshot without extra I2C or key acknowledgements.
 */
#pragma once
#include "ui_battery.h"
#if !READER_METALIO
#include "read_pico_pmu.h"
#endif
static inline void reader_battery_sample(ui_battery_t* battery) {
#if READER_METALIO
    // 无名电量适配待单独验证，未知值不伪装成满电。/ Keep unavailable Metalio telemetry unknown until its adapter is validated.
    ui_battery_update(battery,false,0,false);
#else
    const pmu_snapshot_t* s=read_pico_pmu_get();
    ui_battery_update(battery,s && s->present && s->status_ok && (s->flags & PMU_STATUS_BATTERY_VALID) && s->last_err==ESP_OK,
        s?s->soc_permille:0,s && s->charge_state==PMU_CHARGE_CHARGING);
#endif
}
