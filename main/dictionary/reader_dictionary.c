/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 二分查询 SD 词典，仅解析命中记录。/ Binary search an SD dictionary and parse only the hit.
 * 冻结：不挂载、不修改介质。/ Frozen: never mount or modify media.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "reader_dictionary.h"
#include "cJSON.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef READER_DICT_DIRECTORY
#define READER_DICT_DIRECTORY "/sdcard/dict"
#endif

enum { HEADER = 32, INDEX = 80, KEY = 64, MAX_RECORD = 16384 };

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static size_t utf8_width(const unsigned char *p, size_t n)
{
    if (!n || !p[0]) return 0;
    if (p[0] < 0x80) return 1;
    if (p[0] < 0xc2 || p[0] > 0xf4) return 0;
    size_t w = p[0] < 0xe0 ? 2 : p[0] < 0xf0 ? 3 : 4;
    if (n < w) return 0;
    for (size_t i = 1; i < w; ++i)
        if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return w;
}

static bool normalize(const char *query, char key[KEY])
{
    const unsigned char *p = (const unsigned char *)query;
    size_t remaining = strlen(query), used = 0;
    bool space = false;
    while (remaining) {
        size_t w = utf8_width(p, remaining);
        if (!w) return false;
        if (*p == ' ' || (*p >= 9 && *p <= 13)) {
            space = used != 0;
        } else {
            bool apostrophe = w == 3 && p[0] == 0xe2 && p[1] == 0x80 &&
                              (p[2] == 0x98 || p[2] == 0x99);
            if (used + space + (apostrophe ? 1 : w) >= KEY) return false;
            if (space) key[used++] = ' ';
            space = false;
            if (apostrophe) key[used++] = '\'';
            else if (*p >= 'A' && *p <= 'Z') key[used++] = (char)(*p + 'a' - 'A');
            else { memcpy(key + used, p, w); used += w; }
        }
        remaining -= w;
        p += w;
    }
    key[used] = 0;
    return used != 0;
}

// 限制递归深度并验证字符串原始字节。/ Limit parser recursion and validate raw string bytes.
static bool safe_json(const unsigned char *p, size_t n)
{
    bool string = false;
    unsigned depth = 0;
    for (size_t i = 0; i < n;) {
        size_t w = utf8_width(p + i, n - i);
        if (!w) return false;
        unsigned char c = p[i];
        i += w;
        if (c == '"') string = !string;
        else if (string && c == '\\') {
            if (i == n || !strchr("\"\\/bfnrtu", p[i]) || p[i] == 0) return false;
            if (p[i++] == 'u') {
                if (n-i < 4 || !memcmp(p+i, "0000", 4)) return false;
                for (size_t j = 0; j < 4; ++j) {
                    unsigned char h = p[i++];
                    if (!(h >= '0' && h <= '9') && !(h >= 'a' && h <= 'f') &&
                        !(h >= 'A' && h <= 'F')) return false;
                }
            }
        } else if (c < 32 && (string || (c != '\n' && c != '\r' && c != '\t'))) return false;
        else if (!string && (c == '[' || c == '{')) { if (++depth > 8) return false; }
        else if (!string && (c == ']' || c == '}')) { if (!depth) return false; --depth; }
    }
    return !string && !depth;
}

static void copy_field(char *dest, size_t cap, const char *value, bool *truncated)
{
    size_t n = strlen(value);
    if (n >= cap) {
        n = cap - 1;
        while (((unsigned char)value[n] & 0xc0) == 0x80) --n;
        *truncated = true;
    }
    memcpy(dest, value, n);
    dest[n] = 0;
}

static bool parse_entry(cJSON *root, reader_dict_entry_t *out)
{
    const char *word = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "word"));
    cJSON *source_item = cJSON_GetObjectItemCaseSensitive(root, "source");
    const char *source = cJSON_GetStringValue(source_item);
    if (!word || !*word) return false;
    if (source_item) {
        if (!source || !*source || strlen(source) >= sizeof out->source) return false;
        strcpy(out->source, source);
    }
    // 新词卡只接收简明义项，不转换旧的受限原文库。/ Concise cards reject the retired verbatim format.
    if (cJSON_GetObjectItemCaseSensitive(root, "full_text")) return false;
    const char *pron = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "pronunciation"));
    cJSON *senses = cJSON_GetObjectItemCaseSensitive(root, "senses");
    if (!pron || !cJSON_IsArray(senses) || !senses->child) return false;
    copy_field(out->word, sizeof out->word, word, &out->truncated);
    copy_field(out->pronunciation, sizeof out->pronunciation, pron, &out->truncated);
    cJSON *s;
    cJSON_ArrayForEach(s, senses) {
        const char *pos = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(s, "pos"));
        const char *gloss = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(s, "gloss"));
        cJSON *syn = cJSON_GetObjectItemCaseSensitive(s, "synonyms");
        if (!pos || !gloss || !cJSON_IsArray(syn)) return false;
        cJSON *sense_pron_item=cJSON_GetObjectItemCaseSensitive(s,"pronunciation");
        const char *sense_pron=cJSON_GetStringValue(sense_pron_item);
        if(sense_pron_item && !sense_pron) return false;
        reader_dict_sense_t *dst = NULL;
        if (out->count < sizeof out->senses / sizeof out->senses[0]) {
            dst = &out->senses[out->count++];
            if(sense_pron) copy_field(dst->pronunciation,sizeof dst->pronunciation,sense_pron,&out->truncated);
            copy_field(dst->pos, sizeof dst->pos, pos, &out->truncated);
            copy_field(dst->gloss, sizeof dst->gloss, gloss, &out->truncated);
        } else out->truncated = true;
        size_t used = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, syn) {
            const char *name = cJSON_GetStringValue(item);
            if (!name) return false;
            if (!dst || !*name) continue;
            size_t len = strlen(name), sep = used ? 2 : 0;
            if (used + sep + len >= sizeof dst->synonyms) {
                out->truncated = true;
                dst = NULL;
                continue;
            }
            if (sep) memcpy(dst->synonyms + used, ", ", sep);
            memcpy(dst->synonyms + used + sep, name, len + 1);
            used += len + sep;
        }
    }
    return true;
}

static reader_dict_status_t read_at(FILE *f, long offset, void *out, size_t len)
{
    if (fseek(f, offset, SEEK_SET)) return READER_DICT_IO_ERROR;
    if (fread(out, 1, len, f) == len) return READER_DICT_OK;
    return ferror(f) ? READER_DICT_IO_ERROR : READER_DICT_INVALID;
}

static reader_dict_status_t search(FILE *f, const char *key, reader_dict_entry_t *entry)
{
    unsigned char h[HEADER], idx[INDEX];
    // 文件大小来自已打开文件的元数据，避免为此遍历整个 FAT 簇链。
    // Read size from open-file metadata instead of walking the entire FAT chain to EOF.
    struct stat info;
    if (fstat(fileno(f), &info) || info.st_size < 0) return READER_DICT_IO_ERROR;
    uint64_t end = (uint64_t)info.st_size;
    reader_dict_status_t status = read_at(f, 0, h, sizeof h);
    if (status != READER_DICT_OK) return status;
    uint32_t count = le32(h + 12), data = le32(h + 20), size = le32(h + 24);
    if (memcmp(h, "SORDICT1", 8) || le32(h+8) != 1 || le32(h+16) != HEADER ||
        HEADER + (uint64_t)count * INDEX != data || data > size ||
        (uint64_t)end != size || le32(h+28)) return READER_DICT_INVALID;
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi-lo)/2;
        status = read_at(f, HEADER + mid * INDEX, idx, sizeof idx);
        if (status != READER_DICT_OK) return status;
        size_t k = 0;
        while (k < KEY && idx[k]) {
            size_t w = utf8_width(idx+k, KEY-k);
            if (!w) return READER_DICT_INVALID;
            k += w;
        }
        if (!k || k == KEY) return READER_DICT_INVALID;
        for (; k < KEY; ++k) if (idx[k]) return READER_DICT_INVALID;
        uint32_t offset = le32(idx+64), length = le32(idx+72);
        if (le32(idx+68) || le32(idx+76) || offset < data || offset > size ||
            !length || length > MAX_RECORD || length > size-offset) return READER_DICT_INVALID;
        int cmp = strcmp(key, (const char *)idx);
        if (cmp < 0) hi = mid;
        else if (cmp > 0) lo = mid+1;
        else {
            char *record = malloc((size_t)length + 1);
            if (!record) return READER_DICT_IO_ERROR;
            status = read_at(f, offset, record, length);
            if (status == READER_DICT_OK) {
                record[length] = 0;
                cJSON *json = safe_json((unsigned char *)record, length)
                              ? cJSON_ParseWithLengthOpts(record, length+1, NULL, true) : NULL;
                if (!json || !parse_entry(json, entry)) status = READER_DICT_INVALID;
                cJSON_Delete(json);
            }
            free(record);
            return status;
        }
    }
    return READER_DICT_NOT_FOUND;
}

// 精确词条优先；仅为英文所有格生成一次候选，不盲目截去其他词尾。
// Exact entries win; try one English possessive base, never arbitrary suffix stemming.
static bool possessive_base(const char *key, char base[KEY])
{
    static const char *const contractions[] = {
        "it's", "he's", "she's", "that's", "there's", "what's", "who's",
        "where's", "when's", "why's", "how's", "let's", "here's"
    };
    size_t n = strlen(key), keep;
    if (n > 2 && key[n-2] == '\'' && key[n-1] == 's') keep = n-2;
    else if (n > 2 && key[n-2] == 's' && key[n-1] == '\'') keep = n-1;
    else return false;
    for (size_t i = 0; i < sizeof contractions / sizeof contractions[0]; ++i)
        if (!strcmp(key, contractions[i])) return false;
    for (size_t i = 0; i < keep; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (c >= 'a' && c <= 'z') continue;
        if ((c != ' ' && c != '-' && c != '\'') || !i || i+1 == keep ||
            key[i-1] < 'a' || key[i-1] > 'z' ||
            key[i+1] < 'a' || key[i+1] > 'z') return false;
    }
    memcpy(base, key, keep);
    base[keep] = 0;
    return true;
}

reader_dict_status_t reader_dictionary_session_open(reader_dict_session_t *session,const char *path)
{
    if(!session || session->file)return READER_DICT_INVALID;
    FILE *f=fopen(path?path:READER_DICT_DIRECTORY "/offline.dict","rb");
    session->file=f;
    session->status=f?READER_DICT_OK:(errno==ENOENT || errno==ENOTDIR?READER_DICT_MISSING:READER_DICT_IO_ERROR);
    return session->status;
}

reader_dict_status_t reader_dictionary_session_lookup(reader_dict_session_t *session,const char *query,
                                                       reader_dict_entry_t *entry)
{
    if(!entry)return READER_DICT_INVALID;
    memset(entry,0,sizeof(*entry));
    if(!session || !query)return READER_DICT_INVALID;
    if(!session->file)return session->status==READER_DICT_OK?READER_DICT_IO_ERROR:session->status;
    if(session->status!=READER_DICT_OK)return session->status;
    char key[KEY],base[KEY];
    if(!normalize(query,key))return READER_DICT_NOT_FOUND;
    FILE *f=session->file;
    reader_dict_status_t status=search(f,key,entry);
    if(status==READER_DICT_NOT_FOUND && possessive_base(key,base))status=search(f,base,entry);
    if(status==READER_DICT_IO_ERROR)session->status=status;
    if(status!=READER_DICT_OK)memset(entry,0,sizeof(*entry));
    return status;
}

reader_dict_status_t reader_dictionary_session_close(reader_dict_session_t *session)
{
    if(!session)return READER_DICT_INVALID;
    FILE *f=session->file;session->file=NULL;session->status=READER_DICT_IO_ERROR;
    return f && fclose(f)?READER_DICT_IO_ERROR:READER_DICT_OK;
}

reader_dict_status_t reader_dictionary_lookup_file(const char *path,const char *query,
                                                  reader_dict_entry_t *entry)
{
    if(!entry)return READER_DICT_INVALID;
    memset(entry,0,sizeof(*entry));
    if(!path || !query)return READER_DICT_INVALID;
    char key[KEY];
    if(!normalize(query,key))return READER_DICT_NOT_FOUND;
    reader_dict_session_t session={0};
    reader_dictionary_session_open(&session,path);
    reader_dict_status_t status=reader_dictionary_session_lookup(&session,key,entry);
    if(reader_dictionary_session_close(&session)!=READER_DICT_OK)status=READER_DICT_IO_ERROR;
    if(status!=READER_DICT_OK)memset(entry,0,sizeof(*entry));
    return status;
}

reader_dict_status_t reader_dictionary_lookup(const char *query, reader_dict_entry_t *entry)
{
    return reader_dictionary_lookup_file(READER_DICT_DIRECTORY "/offline.dict", query, entry);
}
