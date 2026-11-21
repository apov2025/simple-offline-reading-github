/*
 * SPDX-License-Identifier: Apache-2.0
 * 固件独占 SD 接口。/ Firmware-only exclusive SD access.
 */
#pragma once
#include <stdbool.h>
#include "sdmmc_cmd.h"

/// 卸载 FAT，独占原始 SD。占用期间禁止应用挂载。
/// / Unmount FAT and claim raw SD; block application mounts while reserved.
esp_err_t read_pico_sd_claim_usb(sdmmc_card_t** result);
bool read_pico_sd_usb_claimed(void);
/// USB 已停止 I/O 后关闭主机；成功后开放重新探测。/ Close the idle host and release for fresh probing on success.
esp_err_t read_pico_sd_end_usb(void);

