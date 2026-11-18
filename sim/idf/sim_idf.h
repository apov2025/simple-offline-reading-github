/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 模拟器用的 ESP-IDF 替身：固件 main/ 用到的 IDF 接口都在这一个头文件里声明，
 * 同目录下的 esp_xxx.h、freertos 头文件 等只是转发到这里。实现见 sim/src/sim_idf.c。
 *
 * Host stand-in for the ESP-IDF APIs used by main/. The sibling esp_xxx.h,
 * freertos headers, ... only forward here. Implemented in sim/src/sim_idf.c.
 */
#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- sdkconfig ---- */
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_IDF_TARGET "esp32s3"
#define CONFIG_SPIRAM 1
#define CONFIG_FREERTOS_HZ 1000
#define CONFIG_READ_PICO_I2C_PORT 0
#define CONFIG_READ_PICO_I2C_SCL_GPIO 40
#define CONFIG_READ_PICO_I2C_SDA_GPIO 39
#define CONFIG_READ_PICO_I2C_FREQ_HZ 400000
#define CONFIG_READ_PICO_IOE_INT_GPIO 41
#define CONFIG_READ_PICO_TP_INT_GPIO 43
#define CONFIG_FCA9555_I2C_TIMEOUT_MS 100

/* ---- esp_attr ---- */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_NOINIT_ATTR
#define WORD_ALIGNED_ATTR
#define FORCE_INLINE_ATTR static inline __attribute__((always_inline))
#define NOINLINE_ATTR __attribute__((noinline))

/* ---- esp_err ---- */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_INVALID_VERSION 0x10A
#define ESP_ERR_INVALID_MAC 0x10B
#define ESP_ERR_NOT_FINISHED 0x10C
#define ESP_ERR_NOT_ALLOWED 0x10D
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
const char* esp_err_to_name(esp_err_t code);
#define ESP_ERROR_CHECK(x) \
    do { \
        esp_err_t sim_rc_ = (x); \
        if (sim_rc_ != ESP_OK) { \
            fprintf(stderr, "ESP_ERROR_CHECK failed: %s at %s:%d\n", esp_err_to_name(sim_rc_), __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) (x)

/* ---- esp_log ---- */
typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;
void esp_log_level_set(const char* tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char* tag);
void sim_log(esp_log_level_t level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(tag, fmt, ...) sim_log(ESP_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) sim_log(ESP_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) sim_log(ESP_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) sim_log(ESP_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) sim_log(ESP_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
#define ESP_DRAM_LOGE ESP_LOGE
#define ESP_DRAM_LOGW ESP_LOGW
#define ESP_DRAM_LOGI ESP_LOGI
#define ESP_LOG_BUFFER_HEX(tag, buf, len) ((void)0)

/* ---- esp_timer ---- */
int64_t esp_timer_get_time(void);

/* ---- esp_heap_caps ---- */
#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps);
void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
void heap_caps_aligned_free(void* ptr);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);

/* ---- esp_system / esp_mac / esp_app_desc ---- */
void esp_restart(void) __attribute__((noreturn));
uint32_t esp_get_free_heap_size(void);
uint32_t esp_random(void);
typedef enum { ESP_MAC_BASE = 0, ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type);
esp_err_t esp_efuse_mac_get_default(uint8_t* mac);
typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    uint8_t app_elf_sha256[32];
} esp_app_desc_t;
const esp_app_desc_t* esp_app_get_description(void);

/* ---- driver/gpio ---- */
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
#define GPIO_NUM_MAX 49
typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2, GPIO_MODE_INPUT_OUTPUT = 3 } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;
typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_POSEDGE,
    GPIO_INTR_NEGEDGE,
    GPIO_INTR_ANYEDGE,
    GPIO_INTR_LOW_LEVEL,
    GPIO_INTR_HIGH_LEVEL,
} gpio_int_type_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t* cfg);
int gpio_get_level(gpio_num_t gpio);
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level);
esp_err_t gpio_wakeup_enable(gpio_num_t gpio, gpio_int_type_t type);
esp_err_t gpio_wakeup_disable(gpio_num_t gpio);

esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode);
esp_err_t gpio_set_intr_type(gpio_num_t gpio, gpio_int_type_t type);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t gpio, void (*fn)(void*), void* arg);
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio);
bool rtc_gpio_is_valid_gpio(gpio_num_t gpio);
esp_err_t rtc_gpio_pulldown_en(gpio_num_t gpio);
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t gpio);
esp_err_t rtc_gpio_pullup_en(gpio_num_t gpio);
esp_err_t rtc_gpio_pullup_dis(gpio_num_t gpio);
#define GPIO_IS_VALID_GPIO(gpio) ((gpio) >= 0 && (gpio) < GPIO_NUM_MAX)
#define GPIO_NUM_0 0
#define GPIO_NUM_1 1
#define GPIO_NUM_2 2
void esp_rom_delay_us(uint32_t us);

/* ---- driver/i2c_master：假总线，每个设备是一张 256 字节的寄存器表。
 * Fake bus: each device is a 256-byte register file. See sim_board.c. ---- */
typedef int i2c_port_t;
typedef struct sim_i2c_bus* i2c_master_bus_handle_t;
typedef struct sim_i2c_dev* i2c_master_dev_handle_t;
typedef enum { I2C_ADDR_BIT_LEN_7 = 0, I2C_ADDR_BIT_LEN_10 = 1 } i2c_addr_bit_len_t;
typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
} i2c_device_config_t;
esp_err_t i2c_master_bus_add_device(
    i2c_master_bus_handle_t bus, const i2c_device_config_t* cfg, i2c_master_dev_handle_t* out
);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t* data, size_t len, int timeout_ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t* data, size_t len, int timeout_ms);
esp_err_t i2c_master_transmit_receive(
    i2c_master_dev_handle_t dev, const uint8_t* wr, size_t wr_len, uint8_t* rd, size_t rd_len,
    int timeout_ms
);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus);

/* ---- esp_sleep ---- */
typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED = 0,
    ESP_SLEEP_WAKEUP_EXT0 = 2,
    ESP_SLEEP_WAKEUP_EXT1 = 3,
    ESP_SLEEP_WAKEUP_TIMER = 4,
    ESP_SLEEP_WAKEUP_GPIO = 7,
} esp_sleep_wakeup_cause_t;
esp_err_t esp_sleep_enable_gpio_wakeup(void);
typedef enum { ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1 } esp_sleep_ext1_wakeup_mode_t;
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask, esp_sleep_ext1_wakeup_mode_t level_mode);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_light_sleep_start(void);
void esp_deep_sleep_start(void) __attribute__((noreturn));
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
esp_err_t sleep_cpu_configure(bool light_sleep_enable);

/* ---- soc/rtc_cntl_reg ---- */
#define RTC_CNTL_OPTION1_REG 0
#define RTC_CNTL_FORCE_DOWNLOAD_BOOT 1
#define REG_WRITE(reg, val) ((void)(reg), (void)(val))

/* ---- FreeRTOS ---- */
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint8_t StackType_t;
#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t) ((uint32_t)(t))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define tskNO_AFFINITY 0x7fffffff
typedef struct sim_task* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
    TaskHandle_t* out, BaseType_t core
);
BaseType_t xTaskCreate(
    TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio, TaskHandle_t* out
);
void vTaskDelete(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks);
#define taskYIELD() vTaskDelay(0)
typedef struct sim_sem* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

/* ---- NVS：键值存到 sim/data/nvs 下的文件里 / stored as files under sim/data/nvs ---- */
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len);
#define SIM_NVS_INT(T, name) \
    esp_err_t nvs_set_##name(nvs_handle_t h, const char* key, T value); \
    esp_err_t nvs_get_##name(nvs_handle_t h, const char* key, T* out);
SIM_NVS_INT(uint8_t, u8)
SIM_NVS_INT(int8_t, i8)
SIM_NVS_INT(uint16_t, u16)
SIM_NVS_INT(int16_t, i16)
SIM_NVS_INT(uint32_t, u32)
SIM_NVS_INT(int32_t, i32)
SIM_NVS_INT(uint64_t, u64)
SIM_NVS_INT(int64_t, i64)
#undef SIM_NVS_INT

/* ---- FAT / 磨损均衡：内部存储映射到 sim/data/flash / internal storage maps to sim/data/flash ---- */
typedef int32_t wl_handle_t;
#define WL_INVALID_HANDLE (-1)
typedef struct {
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
    bool disk_status_check_enable;
    bool use_one_fat;
} esp_vfs_fat_mount_config_t;
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(
    const char* base_path, const char* partition_label, const esp_vfs_fat_mount_config_t* config,
    wl_handle_t* wl_handle
);
esp_err_t esp_vfs_fat_spiflash_unmount_rw_wl(const char* base_path, wl_handle_t wl_handle);
esp_err_t esp_vfs_fat_info(const char* base_path, uint64_t* out_total_bytes, uint64_t* out_free_bytes);

/* ---- espressif/qrcode ---- */
typedef const uint8_t* esp_qrcode_handle_t;
typedef struct {
    void (*display_func)(esp_qrcode_handle_t qrcode);
    int max_qrcode_version;
    int qrcode_ecc_level;
} esp_qrcode_config_t;
#define ESP_QRCODE_ECC_LOW 0
#define ESP_QRCODE_ECC_MED 1
#define ESP_QRCODE_ECC_QUART 2
#define ESP_QRCODE_ECC_HIGH 3
#define ESP_QRCODE_CONFIG_DEFAULT() \
    (esp_qrcode_config_t) { .display_func = NULL, .max_qrcode_version = 10, .qrcode_ecc_level = ESP_QRCODE_ECC_LOW }
esp_err_t esp_qrcode_generate(esp_qrcode_config_t* cfg, const char* text);
int esp_qrcode_get_size(esp_qrcode_handle_t qrcode);
bool esp_qrcode_get_module(esp_qrcode_handle_t qrcode, int x, int y);

#ifdef __cplusplus
}
#endif
