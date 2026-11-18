/* SPDX-License-Identifier: Apache-2.0
 * 适配官方屏幕驱动的 SD 保持释放。/ Adapt the official display driver's SD hold release.
 */
#include "driver/gpio.h"
void MetalioSd_ReleaseHold(void) {
    for (int pin = 20; pin <= 25; pin++) gpio_hold_dis(pin);
}
