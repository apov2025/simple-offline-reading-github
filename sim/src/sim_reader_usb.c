/* SPDX-License-Identifier: Apache-2.0
 * 模拟器不导出主机磁盘，只模拟未连接状态。/ Simulator never exports host disks; model waiting only.
 */
#include "../../main/board/reader_usb.h"
static bool active;
static reader_usb_state_t state;
esp_err_t reader_usb_start(void){active=true;state=READER_USB_WAITING;return ESP_OK;}
bool reader_usb_active(void){return active;}
reader_usb_state_t reader_usb_state(void){return state;}
esp_err_t reader_usb_stop(void){active=false;state=READER_USB_OFF;return ESP_OK;}
esp_err_t reader_usb_stop_confirmed(void){return reader_usb_stop();}
bool reader_usb_requires_restart(void){return false;}
