/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SD 卡离线词典查询。/ Offline SD dictionary lookup.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char pronunciation[192]; ///< 本义项的读音 / Sense-specific pronunciation
    char pos[24];        ///< 词性 / Part of speech
    char gloss[768];     ///< 释义 / Definition
    char synonyms[192];  ///< 本义项的近义词 / Synonyms for this sense
} reader_dict_sense_t;

typedef struct {
    char word[128];                ///< 标题词 / Headword
    char pronunciation[192];       ///< 读音 / Pronunciation
    reader_dict_sense_t senses[12]; ///< 词义 / Senses
    size_t count;                  ///< 已存词义数 / Stored sense count
    bool truncated;                ///< 超出展示容量 / Display capacity exceeded
    char source[96];               ///< 署名来源 / Source attribution
} reader_dict_entry_t;

typedef enum {
    READER_DICT_OK, READER_DICT_NOT_FOUND, READER_DICT_MISSING,
    READER_DICT_INVALID, READER_DICT_IO_ERROR
} reader_dict_status_t;

/// 从默认 SD 词典查询；调用方提供结果空间。/ Look up in the default SD dictionary into caller storage.
reader_dict_status_t reader_dictionary_lookup(const char *query, reader_dict_entry_t *entry);

/// 指定文件查询；返回时已关闭文件。/ Look up in the given file and close it before returning.
reader_dict_status_t reader_dictionary_lookup_file(const char *path, const char *query,
                                                  reader_dict_entry_t *entry);

// A synchronous query group only: initialize to zero and close before returning to the UI.
// Never retain a session across refresh, sleep, media changes or USB ownership.
#define READER_DICT_SESSION_API 1
typedef struct {
    void *file;
    reader_dict_status_t status;
} reader_dict_session_t;
reader_dict_status_t reader_dictionary_session_open(reader_dict_session_t *session,const char *path);
reader_dict_status_t reader_dictionary_session_lookup(reader_dict_session_t *session,const char *query,reader_dict_entry_t *entry);
reader_dict_status_t reader_dictionary_session_close(reader_dict_session_t *session);
