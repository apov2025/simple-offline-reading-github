# 官方 S31 后端独立选源，不编入 S3 汇编。/ Select the official S31 backend without S3 assembly.
set(metalio_root "${CMAKE_CURRENT_LIST_DIR}")
set(epd "${metalio_root}/upstream/display")
set(bus "${metalio_root}/upstream/bus")
idf_component_register(
    SRCS "${epd}/epdiy.c" "${epd}/render.c" "${epd}/highlevel.c" "${epd}/font.c"
        "${epd}/displays.c" "${epd}/epd_board.c" "${epd}/epd_board_metalio_eink4_plus.c"
        "${epd}/epdiy_riscv_ve_stub.c" "${epd}/board_specific.c"
        "${epd}/output_lcd/lcd_driver.c" "${epd}/output_lcd/render_lcd.c"
        "${epd}/output_common/lut.c" "${epd}/output_common/line_queue.c"
        "${epd}/output_common/render_context.c" "${epd}/output_common/render_method.c"
        "${epd}/waveforms/my_waveform.c" "${epd}/waveforms/my_waveform_trim.c"
        "${bus}/metalio_i2c1.c" "${bus}/pca9555.c" "${bus}/tps65185.c"
        "${metalio_root}/epd_compat.c"
    INCLUDE_DIRS "${epd}" "${epd}/output_lcd" "${epd}/output_common" "${epd}/waveforms"
        "${bus}" "${metalio_root}/upstream" "${metalio_root}/include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_driver_dma esp_lcd esp_timer
    PRIV_REQUIRES esp_hw_support esp_rom esp_hal_dma esp_hal_rmt esp_hal_lcd esp_hal_i2s soc esp_mm)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-format)
