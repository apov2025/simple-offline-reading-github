set(main_requires
    read_pico
    read_pico_transfer
    read_pico_search
    espressif__qrcode
    espressif__cjson
    espressif__esp_tinyusb
    esp_hal_usb
    continuous_du
    e0470_epaper_waveform
    read_pico_pmu
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_wifi
    esp_netif
    esp_event
    lwip
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_app_format
)
set(main_sources
        "app_main.c"
        "display.c"
        "sleep.c"
        "settings.c"
        "board/reader_controls.c"
        "board/reader_screenshot.c"
        "board/reader_wallpaper.c"
        "board/reader_clock.c"
        "app/app_loop.c"
        "app/app_registry.c"
        "ui/ui_kit.c"
        "ui/ui_power_screen.c"
        "ui/ui_battery.c"
        "ui/ui_device_panel.c"
        "ui/ui_gesture.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "apps/app_reader.c"
        "apps/app_wifi_transfer.c"
        "apps/app_usb_transfer.c"
        "board/reader_usb.c"
        "annotations/reader_highlight_store.c"
        "annotations/reader_highlight_ui.c"
        "annotations/reader_sentence.c"
        "dictionary/reader_dictionary.c"
        "dictionary/reader_dictionary_ui.c"
        "dictionary/reader_vocabulary.c"
        "dictionary/reader_vocabulary_ui.c"
        "book/book_store.c"
        "book/book_progress.c"
        "book/book_visibility.c"
        "book/book_source.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_image.c"
        "book/vendor/tjpgd.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/hyphen_en.c"
        "book/gbk.c"
        "font/ttf_font.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c")

if(READER_METALIO OR IDF_TARGET STREQUAL "esp32s31")
    set(main_requires epdiy sc7a20h read_pico_search fatfs wear_levelling bootloader_support spi_flash heap nvs_flash
        esp_hw_support esp_rom esp_driver_gpio esp_driver_ledc esp_driver_usb_serial_jtag esp_app_format esp_driver_sdmmc sdmmc)
    list(REMOVE_ITEM main_sources "app_main.c" "display.c" "sleep.c" "ui/ui_wifi_qr.c"
        "apps/app_home.c" "apps/app_refresh.c" "apps/app_reading.c" "apps/app_book.c" "apps/app_transfer.c" "apps/app_wifi_transfer.c" "apps/app_usb_transfer.c" "board/reader_usb.c" "board/reader_wallpaper.c" "board/reader_clock.c" "annotations/reader_highlight_store.c" "annotations/reader_highlight_ui.c" "annotations/reader_sentence.c" "dictionary/reader_dictionary.c" "dictionary/reader_dictionary_ui.c" "dictionary/reader_vocabulary.c" "dictionary/reader_vocabulary_ui.c"
        "apps/app_touch.c" "apps/app_axis.c" "apps/app_power.c" "apps/app_ioe.c" "apps/app_pmu.c"
        "apps/app_sd.c" "apps/app_font_pick.c" "apps/app_sleep.c" "apps/app_selftest.c" "apps/app_key.c"
        "factory/pmu_selftest.c" "factory/vcom_setup.c")
    list(APPEND main_sources "board/metalio_main.c" "board/metalio_board.c" "board/metalio_media.c"
        "board/metalio_display.c" "board/metalio_sleep.c")
endif()
idf_component_register(SRCS ${main_sources}
    REQUIRES ${main_requires}
    INCLUDE_DIRS "." "board" "app" "apps" "font" "ui" "factory" "book" "dictionary"
    EMBED_FILES
        "assets/lock_4bpp.bin"
        "assets/loading_4bpp.bin"
        "assets/builtin.ttf"
        "assets/reading.md"
)

# 机身自带的字体和书：把 builtin/storage 打成 storage 分区的 FAT 镜像，随 idf.py flash 一起烧。
# 只烧程序、不动这个分区用 idf.py app-flash（留在机身里的上传书不会被覆盖）。
# Built-in font and books: builtin/storage becomes the storage partition's FAT image and is flashed by
# idf.py flash. Use idf.py app-flash to update the app alone and leave this partition (and any books
# uploaded into it) untouched.
if(READER_METALIO OR IDF_TARGET STREQUAL "esp32s31")
    fatfs_create_spiflash_image(storage ${CMAKE_SOURCE_DIR}/builtin/storage FLASH_IN_PROJECT)
else()
    # 只给小纸镜像加入说明书；暂存目录每次从源重建。/ Add the guide only to Pico images; rebuild staging from sources each time.
    set(pico_storage_dir ${CMAKE_BINARY_DIR}/pico-storage)
    add_custom_target(stage_pico_storage
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${pico_storage_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/builtin/storage ${pico_storage_dir}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${pico_storage_dir}/books
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/builtin/pico ${pico_storage_dir}
        VERBATIM)
    fatfs_create_spiflash_image(storage ${pico_storage_dir} FLASH_IN_PROJECT)
    add_dependencies(fatfs_storage_bin stage_pico_storage)
endif()

if(READER_METALIO OR IDF_TARGET STREQUAL "esp32s31")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC READER_METALIO=1 METALIO_PIN_PROFILE="${METALIO_PIN_PROFILE}")
    if(DEFINED METALIO_VCOM_MV)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE METALIO_VCOM_MV=${METALIO_VCOM_MV})
    endif()
endif()

if(READER_METALIO OR IDF_TARGET STREQUAL "esp32s31")
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/boards/metalio/sleep_adapter.cc")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/boards/metalio/upstream/s31_sleep")
    include("${CMAKE_SOURCE_DIR}/boards/metalio/upstream/s31_sleep/s31_sleep_sdk.cmake")
endif()

# 同步 MSC 回调确认写入后才答复，避免默认延迟写入隐藏错误。/ Synchronous MSC callbacks acknowledge only completed writes.
if(NOT READER_METALIO AND IDF_TARGET STREQUAL "esp32s3")
    idf_component_get_property(tinyusb_dir espressif__esp_tinyusb COMPONENT_DIR)
    foreach(callback read10 write10 capacity test_unit_ready start_stop scsi inquiry)
        set_property(SOURCE "${tinyusb_dir}/tinyusb_msc.c" DIRECTORY "${tinyusb_dir}"
            APPEND PROPERTY COMPILE_DEFINITIONS "tud_msc_${callback}_cb=reader_unused_tud_msc_${callback}_cb")
    endforeach()
endif()
