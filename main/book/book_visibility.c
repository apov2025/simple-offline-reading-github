/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 完整路径校验的设备内书籍隐藏状态；版本化小端记录。
 * Device-local book visibility with full-path verification and versioned little-endian records.
 * 冻结：仅使用 rp_hidden；不擦除分区、不修改书籍或进度、不记录路径与标题。
 * Frozen: use only rp_hidden; never erase partitions, change books/progress or log paths/titles.
 */
#include "book_visibility.h"
#include "book_store.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { HEADER_BYTES=8, RECORD_BYTES=HEADER_BYTES+BOOK_STORE_PATH_MAX };
static const char *const NS="rp_hidden";
static bool valid_path(const char *path) {
    return path && *path && strnlen(path,BOOK_STORE_PATH_MAX)<BOOK_STORE_PATH_MAX;
}
static void make_key(const char *path,char key[11]) {
    uint32_t hash=UINT32_C(2166136261);
    for(const unsigned char *p=(const unsigned char *)path;*p;p++)hash=(hash^*p)*UINT32_C(16777619);
    snprintf(key,11,"b_%08" PRIx32,hash);
}
// 缺键正常；已有记录必须完整、已知版本且属于同一条完整路径。
// Missing keys are normal; existing records must be complete, recognized and owned by the exact path.
static esp_err_t read_owned(nvs_handle_t handle,const char *key,const char *path,bool *found) {
    *found=false;
    unsigned char data[RECORD_BYTES];size_t len=sizeof(data);
    esp_err_t err=nvs_get_blob(handle,key,data,&len);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    if(len<HEADER_BYTES+2 || len>sizeof(data) || memcmp(data,"RPH\1",4) || data[4]!=1 || data[5]!=0)
        return ESP_ERR_INVALID_STATE;
    size_t path_bytes=(size_t)data[6]|((size_t)data[7]<<8);
    if(path_bytes!=len-HEADER_BYTES || strnlen((const char *)data+HEADER_BYTES,path_bytes)!=path_bytes-1 ||
       strcmp((const char *)data+HEADER_BYTES,path))return ESP_ERR_INVALID_STATE;
    *found=true;return ESP_OK;
}
esp_err_t book_visibility_get(const char *path,bool *hidden) {
    if(!hidden)return ESP_ERR_INVALID_ARG;
    *hidden=true;
    if(!valid_path(path))return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;esp_err_t err=nvs_open(NS,NVS_READONLY,&handle);
    if(err==ESP_ERR_NVS_NOT_FOUND) { *hidden=false;return ESP_OK; }
    if(err!=ESP_OK)return err;
    char key[11];make_key(path,key);bool found;
    err=read_owned(handle,key,path,&found);nvs_close(handle);
    if(err==ESP_OK)*hidden=found;
    return err;
}
esp_err_t book_visibility_set(const char *path,bool hidden) {
    if(!valid_path(path))return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;esp_err_t err=nvs_open(NS,NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    char key[11];make_key(path,key);bool found;
    err=read_owned(handle,key,path,&found);
    if(err==ESP_OK && found!=hidden) {
        if(hidden) {
            unsigned char data[RECORD_BYTES]={'R','P','H',1,1,0};
            size_t path_bytes=strlen(path)+1;
            data[6]=(unsigned char)path_bytes;data[7]=(unsigned char)(path_bytes>>8);
            memcpy(data+HEADER_BYTES,path,path_bytes);
            err=nvs_set_blob(handle,key,data,HEADER_BYTES+path_bytes);
        } else err=nvs_erase_key(handle,key);
        if(err==ESP_OK)err=nvs_commit(handle);
    }
    nvs_close(handle);return err;
}
