/* SPDX-License-Identifier: Apache-2.0
 * 流式重写单词本，临时文件落盘后以备份换名发布；读取只信完整记录。
 * Stream notebook rewrites, flush a temporary snapshot before backup/rename publication, and read only complete records.
 * 冻结：仅查询动作写入；无后台写入、重挂载或读写 USB 独占的卡。
 * Frozen: only lookup actions write; no background writes, remounting, or I/O during USB ownership.
 */
#include "reader_vocabulary.h"
#include "board/reader_usb.h"
#include "read_pico_sd.h"
#include "cJSON.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef READER_VOCABULARY_DIRECTORY
#define READER_VOCABULARY_DIRECTORY "/sdcard/notes"
#endif
#define VOCAB_FILE READER_VOCABULARY_DIRECTORY "/vocabulary.jsonl"
#define VOCAB_TEMP READER_VOCABULARY_DIRECTORY "/vocabulary.tmp"
#define VOCAB_BACKUP READER_VOCABULARY_DIRECTORY "/vocabulary.bak"
enum { RECORD_BYTES=4096 };
typedef struct { char line[RECORD_BYTES]; reader_vocabulary_record_t row; bool invalid_complete_row; } workspace_t;

static bool media_ready(void) {
    if(reader_usb_active())return false;
    read_pico_sd_info_t info={0};
    return read_pico_sd_get_info(&info)==ESP_OK && info.present && info.mounted;
}
static void copy_utf8(char *out,size_t cap,const char *s) {
    size_t n=strlen(s);
    if(n>=cap) { n=cap-1;while(n && ((unsigned char)s[n]&0xc0)==0x80)--n; }
    memcpy(out,s,n);out[n]=0;
}
// 本地文件也限制解析深度与行长，损坏行不会遮住之后的记录。
// Bound parser depth and line length even for local files; corrupt rows do not hide later records.
static bool parse_row(const char *line,reader_vocabulary_record_t *row) {
    unsigned depth=0;bool quoted=false,escape=false;
    for(const char *p=line;*p;p++) {
        if(escape) { escape=false;continue; }
        if(quoted && *p=='\\') { escape=true;continue; }
        if(*p=='"')quoted=!quoted;
        if(!quoted && (*p=='[' || *p=='{') && ++depth>3)return false;
        if(!quoted && (*p==']' || *p=='}')) { if(!depth)return false;--depth; }
    }
    if(quoted || depth)return false;
    cJSON *json=cJSON_ParseWithOpts(line,NULL,true);
    if(!json)return false;
    const char *query=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"query"));
    const char *book=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"book"));
    cJSON *count=cJSON_GetObjectItemCaseSensitive(json,"count");
    cJSON *result=cJSON_GetObjectItemCaseSensitive(json,"result");
    bool ok=query && *query && strlen(query)<sizeof(row->query) && book && strlen(book)<sizeof(row->book) &&
        cJSON_IsNumber(count) && count->valuedouble>=1 && count->valuedouble<=UINT32_MAX &&
        (double)(uint32_t)count->valuedouble==count->valuedouble && cJSON_IsNumber(result) &&
        result->valuedouble>=READER_DICT_OK && result->valuedouble<=READER_DICT_IO_ERROR &&
        (double)(int)result->valuedouble==result->valuedouble;
    if(ok) {
        strcpy(row->query,query);strcpy(row->book,book);
        row->count=(uint32_t)count->valuedouble;row->result=(reader_dict_status_t)result->valueint;
    }
    cJSON_Delete(json);return ok;
}
static bool next_row(FILE *f,workspace_t *work) {
    size_t n=0;bool bad=false;int ch;
    while((ch=fgetc(f))!=EOF) {
        if(ch=='\n') {
            work->line[n]=0;
            if(!bad && parse_row(work->line,&work->row))return true;
            if(bad || n)work->invalid_complete_row=true;
            n=0;bad=false;
        } else if(ch==0 || n+1>=sizeof(work->line))bad=true;
        else if(!bad)work->line[n++]=(char)ch;
    }
    return false;
}
static FILE *open_current(bool *backup,bool *ok) {
    *backup=false;*ok=true;
    FILE *f=fopen(VOCAB_FILE,"rb");
    if(f || errno!=ENOENT) { if(!f)*ok=false;return f; }
    f=fopen(VOCAB_BACKUP,"rb");
    if(f)*backup=true;
    else if(errno!=ENOENT)*ok=false;
    return f;
}
bool reader_vocabulary_page(size_t skip,reader_vocabulary_record_t *rows,size_t cap,size_t *count,size_t *total) {
    if(!count || !total || (cap && !rows))return false;
    *count=0;*total=0;
    if(!media_ready())return false;
    bool backup,ok;FILE *f=open_current(&backup,&ok);
    if(!f)return ok;
    workspace_t *work=malloc(sizeof(*work));
    if(!work) { fclose(f);return false; }
    work->invalid_complete_row=false;
    while(next_row(f,work)) {
        if(*total>=skip && *count<cap)rows[(*count)++]=work->row;
        ++*total;
    }
    ok=!ferror(f);if(fclose(f)!=0)ok=false;
    free(work);return ok;
}
static bool write_row(FILE *f,const reader_vocabulary_record_t *row) {
    cJSON *json=cJSON_CreateObject();
    if(!json)return false;
    bool ok=cJSON_AddStringToObject(json,"query",row->query) && cJSON_AddStringToObject(json,"book",row->book) &&
        cJSON_AddNumberToObject(json,"count",row->count) && cJSON_AddNumberToObject(json,"result",row->result);
    char *line=ok?cJSON_PrintUnformatted(json):NULL;
    ok=line && fputs(line,f)>=0 && fputc('\n',f)!=EOF;
    cJSON_free(line);cJSON_Delete(json);return ok;
}
bool reader_vocabulary_record(const char *query,const char *book,reader_dict_status_t result) {
    if(!query || !*query || strlen(query)>=sizeof(((reader_vocabulary_record_t *)0)->query) ||
       result<READER_DICT_OK || result>READER_DICT_IO_ERROR || !media_ready())return false;
    workspace_t *work=malloc(sizeof(*work));
    if(!work)return false;
    work->invalid_complete_row=false;
    bool backup,ok;FILE *source=open_current(&backup,&ok),*target=NULL;
    reader_vocabulary_record_t fresh={.count=1,.result=result};
    strcpy(fresh.query,query);copy_utf8(fresh.book,sizeof(fresh.book),book?book:"");
    if(!ok)goto done;
    if(source) {
        while(next_row(source,work))if(!strcmp(work->row.query,query))
            fresh.count=work->row.count==UINT32_MAX?UINT32_MAX:work->row.count+1;
        if(work->invalid_complete_row || ferror(source) || fseek(source,0,SEEK_SET)!=0) { ok=false;goto done; }
    }
    if(mkdir(READER_VOCABULARY_DIRECTORY,0775)!=0 && errno!=EEXIST) { ok=false;goto done; }
    target=fopen(VOCAB_TEMP,"wb");
    if(!target) { ok=false;goto done; }
    ok=write_row(target,&fresh);
    while(ok && source && next_row(source,work))if(strcmp(work->row.query,query))
        ok=fputs(work->line,target)>=0 && fputc('\n',target)!=EOF;
    // 解析失败可能是内存不足；写者必须保留旧版，不能把该行当损坏资料丢弃。
    // A parse failure may mean exhausted memory; writers must preserve the old snapshot rather than discard the row as corrupt.
    if(work->invalid_complete_row || (source && ferror(source)))ok=false;
    if(ok && fflush(target)!=0)ok=false;
    if(ok && fsync(fileno(target))!=0)ok=false;
    if(fclose(target)!=0)ok=false;
    target=NULL;
    if(source && fclose(source)!=0)ok=false;
    source=NULL;
    if(!ok || !media_ready()) { ok=false;goto done; }
    // 发布窗口里保留完整旧版；失败或断电后可从备份读回。
    // Retain a complete prior snapshot during publication, readable after failure or interruption.
    if(!backup) {
        if(unlink(VOCAB_BACKUP)!=0 && errno!=ENOENT) { ok=false;goto done; }
        if(rename(VOCAB_FILE,VOCAB_BACKUP)!=0 && errno!=ENOENT) { ok=false;goto done; }
    }
    if(rename(VOCAB_TEMP,VOCAB_FILE)!=0) { ok=false;goto done; }
    (void)unlink(VOCAB_BACKUP);
done:
    if(target)fclose(target);
    if(source)fclose(source);
    free(work);return ok;
}
