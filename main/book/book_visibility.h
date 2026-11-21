/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 设备内按完整路径保存书籍隐藏状态，不读写书籍与进度。
 * Device-local book visibility by full path, without reading or writing books or progress.
 * 冻结：独立 rp_hidden 命名空间；仅显式隐藏/恢复修改，不随介质移除清空。
 * Frozen: isolated rp_hidden namespace; only explicit hide/restore changes state, never media removal.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

/// 路径必须非空且小于 BOOK_STORE_PATH_MAX 字节（含终止符的容量）；调用限串行 UI 事件。
/// Paths must be nonempty and shorter than BOOK_STORE_PATH_MAX bytes (capacity includes NUL); serialized UI events only.
/// 缺少命名空间或键返回可见；任何错误将 hidden 置 true，调用方必须检查错误并禁止自动入口。
/// Missing namespace/key means visible; errors set hidden true. Callers must check errors and block automatic entry.
esp_err_t book_visibility_get(const char *path, bool *hidden);

/// 显式隐藏或恢复指定路径；恢复仅擦除完整路径匹配的键。损坏或哈希碰撞拒绝覆盖和删除。
/// Explicitly hide or restore one path; restore erases only its verified key. Corruption/hash collisions reject overwrite and deletion.
/// 返回提交错误；不承诺 NVS 失败回滚，调用方仅在成功时更新界面状态。
/// Return commit errors; NVS failure rollback is not guaranteed. Update UI state only on success.
esp_err_t book_visibility_set(const char *path, bool hidden);
