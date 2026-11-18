/* SPDX-License-Identifier: Apache-2.0
 * Metalio 四位 SDMMC，挂载失败不格式化，失效仅标记由调用方关闭消费者。
 * Metalio 4-bit SDMMC; never format failed media, only latch loss until callers close consumers.
 */
#include "reader_media.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
static SemaphoreHandle_t mutex;
static sdmmc_card_t* card;
static reader_media_info_t state;
static bool started;
static int64_t last_probe;
static void mount_worker(void* arg) {
    (void)arg;
    xSemaphoreTake(mutex,portMAX_DELAY);
    sdmmc_host_t host=SDMMC_HOST_DEFAULT(); host.slot=0;host.max_freq_khz=20000;host.command_timeout_ms=500;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width=4;slot.clk=24;slot.cmd=25;slot.d0=20;slot.d1=21;slot.d2=22;slot.d3=23;
    slot.cd=GPIO_NUM_NC;slot.wp=GPIO_NUM_NC;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg={.format_if_mount_failed=false,.max_files=12,.allocation_unit_size=16384};
    state=(reader_media_info_t){.present=true};
    state.error=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&cfg,&card);
    state.mounted=state.error==ESP_OK;
    if (state.mounted) {
        strlcpy(state.name,card->cid.name,sizeof(state.name));
        state.capacity_bytes=(uint64_t)card->csd.capacity*card->csd.sector_size;
        uint64_t total; esp_vfs_fat_info("/sdcard",&total,&state.free_bytes);
    }
    xSemaphoreGive(mutex);vTaskDelete(NULL);
}
esp_err_t reader_media_start_probe(void) {
    if(started)return state.mounted?ESP_OK:state.error;
    if(!mutex)mutex=xSemaphoreCreateMutex();
    if(!mutex)return ESP_ERR_NO_MEM;
    started=true;state=(reader_media_info_t){.present=true,.error=ESP_ERR_NOT_FINISHED};
    if(xTaskCreate(mount_worker,"metalio_sd",4096,NULL,3,NULL)!=pdPASS) {
        started=false;state.error=ESP_ERR_NO_MEM;return state.error;
    }
    return ESP_ERR_NOT_FINISHED;
}
esp_err_t reader_media_get_info(reader_media_info_t* info) {
    if(!info)return ESP_ERR_INVALID_ARG;
    if(!started){*info=(reader_media_info_t){.error=ESP_ERR_INVALID_STATE};return ESP_ERR_INVALID_STATE;}
    if(xSemaphoreTake(mutex,0)!=pdTRUE) {
        *info=(reader_media_info_t){.present=true,.error=ESP_ERR_NOT_FINISHED};return ESP_ERR_NOT_FINISHED;
    }
    int64_t now=esp_timer_get_time()/1000;
    if(state.mounted && now-last_probe>=1000) {
        last_probe=now;esp_err_t err=sdmmc_get_status(card);
        if(err!=ESP_OK){state.mounted=false;state.error=err;state.capacity_bytes=state.free_bytes=0;}
    }
    *info=state;xSemaphoreGive(mutex);return info->error;
}
esp_err_t reader_media_sync(void) {
    if(!mutex)return ESP_OK;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(20))!=pdTRUE)return ESP_ERR_NOT_FINISHED;
    esp_err_t err=card?esp_vfs_fat_sdcard_unmount("/sdcard",card):ESP_OK;
    if(err==ESP_OK){card=NULL;started=false;state=(reader_media_info_t){0};}
    xSemaphoreGive(mutex);return err;
}
esp_err_t reader_media_remount(void) {
    esp_err_t err=reader_media_sync();if(err!=ESP_OK)return err;
    return reader_media_start_probe();
}
