/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读器产品页清单；首页与旧接口均返回书库。
 *
 * Reader product page table; home and legacy aliases always return the library.
 */

#include "app_registry.h"

extern const app_desc_t app_reader;
#if !READER_METALIO
extern const app_desc_t app_wifi_transfer;
extern const app_desc_t app_usb_transfer;
#endif

// 仅注册阅读器产品页，demo 不进入产品构建。/ Register product pages only; demos are excluded from product builds.
static const app_desc_t* const s_apps[] = {
    &app_reader,
#if !READER_METALIO
    &app_wifi_transfer,
    &app_usb_transfer,
#endif
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

int app_count(void) {
    return APP_COUNT;
}

const app_desc_t* app_at(int index) {
    if (index < 0 || index >= APP_COUNT) return NULL;
    return s_apps[index];
}

int app_index_of(const app_desc_t* app) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_apps[i] == app) return i;
    }
    return -1;
}

const app_desc_t* app_reader_page(void) {
    return &app_reader;
}

const app_desc_t* app_home_page(void) {
    return &app_reader;
}

const app_desc_t* app_selftest_page(void) {
    return &app_reader;
}
