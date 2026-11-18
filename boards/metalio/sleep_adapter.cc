/* SPDX-License-Identifier: Apache-2.0
 * C 阅读循环与官方 S31 休眠兼容层的桥接。/ Bridge the C reader loop to official S31 sleep compatibility.
 */
#include "s31_sleep.h"
extern "C" void metalio_reader_sleep_init(void) {
    s31_sleep::CaptureReset();
    s31_sleep::Init();
}
extern "C" esp_err_t metalio_reader_light_wait(void) {
    s31_sleep::BeginCycle();
    return s31_sleep::LightSleep(0);
}
