/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 本地划线存储；正文缓存和绘制由调用方负责。
 * Local highlight persistence; callers own chapter caches and rendering.
 * 冻结：仅显式保存或取消划线写卡；不联网、不挂载或格式化介质。
 * Frozen: write only on explicit save or removal; no network, media mounting or formatting.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t start; ///< 章内 UTF-8 起始字节（含）。/ Inclusive chapter UTF-8 byte offset.
    uint32_t end;   ///< 章内 UTF-8 结束字节（不含）。/ Exclusive chapter UTF-8 byte offset.
} reader_highlight_range_t;

/// 读取并核对书籍、章节指纹及原文；无记录成功返回零条。容量不足或错误返回 false 并清零 count。
/// Load verified book/chapter/quote matches; missing records succeed with zero count. Errors/overflow return false and clear count.
/// 调用方在绘制和拖动之外缓存结果；失败时不使用 out 内容。/ Cache outside drawing/dragging; ignore out on failure.
bool reader_highlight_load(const char *path, uint32_t file_bytes, size_t chapter,
                           const char *text, size_t len, reader_highlight_range_t *out,
                           size_t capacity, size_t *count);

/// 保存精确半开区间（至多 8192 字节），重复记录成功但不重复写入；失败保留已完成记录。
/// Save an exact half-open range (at most 8192 bytes); duplicates succeed without another row; failure preserves completed records.
/// JSONL 新记录含 created_at:null，明确表示未可靠校时；旧记录缺少该字段同样表示时间未知。
/// New JSONL rows contain created_at:null for an uncalibrated clock; legacy missing fields also mean unknown time.
/// 不根据系统时钟猜测日期；旧时间元数据与未知扩展原样保留，重复保存不改创建时间。
/// Never infer dates from the system clock; preserve existing time metadata/extensions and do not change creation time on duplicate saves.
/// 路径最多 1024 字节，两个标题各最多 512 字节；超过限制不截断。仅适用于串行 UI 事件。
/// Paths are at most 1024 bytes, titles 512 bytes each; reject rather than truncate. Serialized UI events only.
bool reader_highlight_save(const char *path, const char *title, uint32_t file_bytes,
                           size_t chapter, const char *chapter_title, const char *text,
                           size_t len, size_t start, size_t end);

/// 调用方只传可信 UTC 秒；0 表示未校时并写 null，不得传未校准系统时钟或旧缓存时间。
/// Caller supplies trusted UTC epoch seconds only; 0 writes null. Never pass an uncalibrated system clock or stale cached time.
/// 有效范围为 1..253402300799（9999 年末），以 YYYY-MM-DDTHH:MM:SSZ 保存；超限失败且不改文件。
/// Accept 1..253402300799 (end of year 9999), storing YYYY-MM-DDTHH:MM:SSZ; reject out-of-range values without changing files.
/// 重复记录保留首次创建时间（包括 null 或旧格式缺失），不补写为本次时间。
/// Duplicates preserve their first creation time, including null or a legacy missing field; never backfill the new time.
bool reader_highlight_save_at(const char *path, const char *title, uint32_t file_bytes,
                              size_t chapter, const char *chapter_title, const char *text,
                              size_t len, size_t start, size_t end, int64_t trusted_epoch);

/// 从全部匹配当前书籍/章节原文的记录扣除半开区间；全覆盖删除、部分覆盖裁剪或拆分。
/// Subtract a half-open interval from every verified current-book/chapter row; remove, trim or split overlaps.
/// 保留剩余空白、首次创建时间与扩展元数据；无重叠成功不写，超限或失败保留旧文件。
/// Preserve remaining whitespace, original creation time and extension metadata; no-op succeeds without writing, limits/errors preserve the old file.
bool reader_highlight_remove(const char *path, uint32_t file_bytes, size_t chapter,
                             const char *text, size_t len, size_t start, size_t end);
