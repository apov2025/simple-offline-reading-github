/* SPDX-License-Identifier: Apache-2.0
 * SD 单词本：按查询原文合并，最近在前，不记录时间。/ SD notebook: coalesce exact queries, newest first, without timestamps.
 */
#pragma once
#include "reader_dictionary.h"
#include <stdint.h>

typedef struct {
    char query[256]; ///< 查询原文 / Exact queried text
    char book[192]; ///< 最近查询的书名 / Latest source book title
    uint32_t count; ///< 累计显式查询次数 / Explicit lookup count
    reader_dict_status_t result; ///< 最近查询结果 / Latest lookup result
} reader_vocabulary_record_t;

/// 只由显式查询调用；完成所有写入后返回，失败保留旧文件，不挂载介质。/ Explicit queries only; finish writes before returning, preserve the old file on failure, never mount media.
bool reader_vocabulary_record(const char *query,const char *book,reader_dict_status_t result);
/// 完整坏行仍可跳过阅读，但为避免解析内存不足丢资料，存在坏行时拒绝写回。/ Complete bad rows may be skipped for reading, but block writes to avoid losing data on parser allocation failure.
/// 常量内存分页读；空文件成功，无法读卡返回 false。/ Read a page with constant memory; absent file is empty, unavailable media returns false.
bool reader_vocabulary_page(size_t skip,reader_vocabulary_record_t *rows,size_t capacity,size_t *count,size_t *total);
