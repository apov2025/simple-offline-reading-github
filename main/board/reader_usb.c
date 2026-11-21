/*
 * SPDX-License-Identifier: Apache-2.0
 * 仅导出已独占的 TF 卡；USB 任务同步完成每次写入后才向电脑报告成功。
 * Export only the reserved SD card; acknowledge writes after the USB task completes them.
 */
#include "reader_usb.h"
#include "esp_attr.h"
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "hal/usb_serial_jtag_ll.h"
#include "read_pico_sd_usb.h"
#include "tinyusb_default_config.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static reader_usb_state_t state;
static enum { HOST_WAITING, HOST_OWNS, HOST_EJECTED } host;
static bool stopping, busy, installed, phy_touched, serial_pad_enabled, teardown_failed;
static bool install_failed;
static sdmmc_card_t *medium;
static uint8_t partial[512] __attribute__((aligned(4)));
// One bounded DMA-capable readback buffer; never shares storage with the host payload.
static DMA_ATTR uint8_t verified[4096];

static esp_err_t write_checked(const void *data,size_t block,size_t blocks) {
    esp_err_t err=sdmmc_write_sectors(medium,data,block,blocks);
    if(err!=ESP_OK || blocks==1)return err;
    // IDF can mask a failed multi-block write with ACMD22 success. Require exact readback.
    err=sdmmc_read_sectors(medium,verified,block,blocks);
    if(err==ESP_OK && memcmp(verified,data,blocks*sizeof(partial))!=0)err=ESP_FAIL;
    return err;
}

static void fault(void) {
    portENTER_CRITICAL(&lock);
    state = READER_USB_ERROR;
    portEXIT_CRITICAL(&lock);
}

static void attached(void) {
    portENTER_CRITICAL(&lock);
    if (!stopping && state != READER_USB_OFF && host == HOST_WAITING) {
        host = HOST_OWNS;
        if (state == READER_USB_WAITING) state = READER_USB_CONNECTED;
    }
    portEXIT_CRITICAL(&lock);
}

static void usb_event(tinyusb_event_t *event, void *arg) {
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED) attached();
}

bool reader_usb_active(void) {
    return read_pico_sd_usb_claimed();
}

reader_usb_state_t reader_usb_state(void) {
    if (reader_usb_active() && !read_pico_sd_present()) fault();
    portENTER_CRITICAL(&lock);
    reader_usb_state_t result = state;
    portEXIT_CRITICAL(&lock);
    return result;
}

esp_err_t reader_usb_start(void) {
    if (reader_usb_active()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = read_pico_sd_claim_usb(&medium);
    if (err == ESP_OK && (medium->csd.sector_size != sizeof(partial) || !medium->csd.capacity))
        err = ESP_ERR_NOT_SUPPORTED;
    if (err == ESP_OK) {
        tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_event);
        portENTER_CRITICAL(&lock);
        state = READER_USB_WAITING;
        portEXIT_CRITICAL(&lock);
        // 给已连接电脑一个可识别的断开间隔。/ Let a connected host observe Serial/JTAG detach.
        serial_pad_enabled = usb_serial_jtag_ll_phy_is_pad_enabled();
        phy_touched = true;
        usb_serial_jtag_ll_phy_enable_pad(false);
        vTaskDelay(pdMS_TO_TICKS(120));
        err = tinyusb_driver_install(&config);
        installed = err == ESP_OK;
        install_failed = !installed;
    }
    if (err != ESP_OK) fault();
    return err;
}

// 所有 MSC 回调由同一 USB 任务串行执行。/ All MSC callbacks run serially on the USB task.
static bool begin_io(uint8_t lun) {
    attached();
    portENTER_CRITICAL(&lock);
    bool ready = lun == 0 && !stopping && state == READER_USB_CONNECTED;
    if (ready) busy = true;
    portEXIT_CRITICAL(&lock);
    if (ready && !read_pico_sd_present()) {
        portENTER_CRITICAL(&lock);
        state = READER_USB_ERROR;
        busy = false;
        portEXIT_CRITICAL(&lock);
        ready = false;
    }
    if (!ready) tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return ready;
}

static void end_io(esp_err_t err) {
    portENTER_CRITICAL(&lock);
    if (err != ESP_OK) state = READER_USB_ERROR;
    busy = false;
    portEXIT_CRITICAL(&lock);
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t rev[4]) {
    (void)lun;
    attached();
    memcpy(vendor, "Read    ", 8);
    memcpy(product, "Pico SD Card    ", 16);
    memcpy(rev, "1.00", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (!begin_io(lun)) return false;
    end_io(ESP_OK);
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size) {
    *count = 0;
    *size = 0;
    if (!begin_io(lun)) return;
    *count = medium->csd.capacity;
    *size = medium->csd.sector_size;
    end_io(ESP_OK);
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool eject) {
    (void)power;
    if (!begin_io(lun)) {
        // 故障介质已停止 I/O；电脑仍可释放占用。/ A failed medium rejects data but can still be ejected.
        portENTER_CRITICAL(&lock);
        bool release = lun == 0 && !start && eject && !stopping && !busy
            && state == READER_USB_ERROR && host == HOST_OWNS;
        if (release) host = HOST_EJECTED;
        portEXIT_CRITICAL(&lock);
        return release;
    }
    esp_err_t err = ESP_OK;
    if (!start && eject) {
        err = sdmmc_get_status(medium);
        portENTER_CRITICAL(&lock);
        if (err == ESP_OK) {
            host = HOST_EJECTED;
            if (state == READER_USB_CONNECTED) state = READER_USB_EJECTED;
        }
        portEXIT_CRITICAL(&lock);
    }
    end_io(err);
    if (err != ESP_OK) tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
    return err == ESP_OK;
}

static int32_t transfer(uint8_t lun, uint32_t lba, uint32_t offset, void *data,
                        uint32_t size, bool write) {
    if (!begin_io(lun)) return -1;
    const size_t sector = sizeof(partial);
    uint64_t pos = (uint64_t)lba * sector + offset;
    if (pos + size > (uint64_t)medium->csd.capacity * sector || size > INT32_MAX) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        end_io(ESP_OK);
        return -1;
    }
    uint8_t *buf = data;
    uint32_t left = size;
    esp_err_t err = ESP_OK;
    while (left && err == ESP_OK) {
        size_t block = pos / sector;
        size_t skip = pos % sector;
        size_t bytes;
        if (!skip && left >= sector) {
            size_t blocks=left/sector;
            if(write && blocks>sizeof(verified)/sector)blocks=sizeof(verified)/sector;
            bytes=blocks*sector;
            err=write?write_checked(buf,block,blocks):sdmmc_read_sectors(medium,buf,block,blocks);
        } else {
            bytes = sector - skip < left ? sector - skip : left;
            err = sdmmc_read_sectors(medium, partial, block, 1);
            if (err == ESP_OK && write) {
                memcpy(partial + skip, buf, bytes);
                err = sdmmc_write_sectors(medium, partial, block, 1);
            } else if (err == ESP_OK) {
                memcpy(buf, partial + skip, bytes);
            }
        }
        pos += bytes;
        buf += bytes;
        left -= bytes;
    }
    if (err != ESP_OK) tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0c : 0x11, 0);
    end_io(err);
    return err == ESP_OK ? (int32_t)size : -1;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t off, void *data, uint32_t size) {
    return transfer(lun, lba, off, data, size, false);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t off, uint8_t *data, uint32_t size) {
    return transfer(lun, lba, off, data, size, true);
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t cmd[16], void *buf, uint16_t size) {
    (void)buf;
    (void)size;
    if (cmd[0] != 0x35) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
        return -1;
    }
    if (!begin_io(lun)) return -1;
    esp_err_t err = sdmmc_get_status(medium);
    if (err != ESP_OK) tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
    end_io(err);
    return err == ESP_OK ? 0 : -1;
}

static void restore_serial(void) {
    if (!phy_touched) return;
    // 交还共享 PHY 给 USB Serial/JTAG。/ Return the shared PHY to USB Serial/JTAG.
    vTaskDelay(pdMS_TO_TICKS(120));
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(serial_pad_enabled);
    phy_touched = false;
}

static esp_err_t stop(bool confirmed) {
    if (!reader_usb_active()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    if (stopping || (host == HOST_OWNS && !confirmed)) {
        portEXIT_CRITICAL(&lock);
        return ESP_ERR_INVALID_STATE;
    }
    stopping = true;
    bool pending = busy;
    portEXIT_CRITICAL(&lock);
    while (pending) {
        vTaskDelay(1);
        portENTER_CRITICAL(&lock);
        pending = busy;
        portEXIT_CRITICAL(&lock);
    }
    // 安装失败时组件未报告内部回滚结果。/ Failed installation does not report its PHY rollback result.
    esp_err_t err = install_failed ? ESP_FAIL : ESP_OK;
    if (installed) {
        err = tinyusb_driver_uninstall();
        if (err == ESP_OK) installed = false;
    }
    restore_serial();
    if (err == ESP_OK) err = read_pico_sd_end_usb();
    portENTER_CRITICAL(&lock);
    if (err == ESP_OK) {
        medium = NULL;
        host = HOST_WAITING;
        state = READER_USB_OFF;
        stopping = false;
    } else {
        // 已部分拆除的驱动不能重试；保留租约，交给重启恢复。/ Keep partially torn-down resources reserved for restart.
        teardown_failed = true;
        state = READER_USB_ERROR;
    }
    portEXIT_CRITICAL(&lock);
    return err;
}

esp_err_t reader_usb_stop(void) { return stop(false); }
esp_err_t reader_usb_stop_confirmed(void) { return stop(true); }

bool reader_usb_requires_restart(void) {
    portENTER_CRITICAL(&lock);
    bool failed = teardown_failed;
    portEXIT_CRITICAL(&lock);
    return failed;
}
