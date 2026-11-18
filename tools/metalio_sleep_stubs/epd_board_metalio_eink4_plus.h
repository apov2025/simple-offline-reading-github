// 仅替换板级硬件边界。/ Replace only the board hardware boundary.
#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t epd_board_metalio_eink4_plus_ioexp_mask_for_sleep(void);
bool epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin(void);
esp_err_t epd_board_metalio_eink4_plus_ioexp_restore_keys(void);
void epd_board_metalio_eink4_plus_bus_hold_low(void);
void epd_board_metalio_eink4_plus_bus_latch_for_sleep(void);
void epd_board_metalio_eink4_plus_bus_restore(void);

void epd_board_metalio_eink4_plus_pwr_key_pulse_train(void);
