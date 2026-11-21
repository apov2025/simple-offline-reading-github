/* SPDX-License-Identifier: Apache-2.0
 * 临时目录内验证持久化、断写恢复和介质保护。/ Check persistence, interrupted writes and media guards inside a temporary directory.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "read_pico_sd.h"
static bool usb_owned,card_ready=true,fail_open,fail_sync,fail_write;
static int rename_failure,rename_calls,filesystem_calls;
bool reader_usb_active(void) { return usb_owned; }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) {
    *info=(read_pico_sd_info_t){.present=card_ready,.mounted=card_ready};return ESP_OK;
}
static FILE *test_fopen(const char *path,const char *mode) {
    ++filesystem_calls;
    if(fail_open && strchr(mode,'w')) { errno=EROFS;return NULL; }
    return fopen(path,mode);
}
static int test_fsync(int fd) { if(fail_sync) { errno=EIO;return -1; }return fsync(fd); }
static int test_fputs(const char *s,FILE *f) { if(fail_write) { errno=ENOSPC;return EOF; }return fputs(s,f); }
static int test_rename(const char *a,const char *b) {
    if(++rename_calls==rename_failure) { errno=EIO;return -1; }return rename(a,b);
}
#define fopen test_fopen
#define fsync test_fsync
#define fputs test_fputs
#define rename test_rename
#include "../main/dictionary/reader_vocabulary.c"
#undef fopen
#undef fsync
#undef fputs
#undef rename
static size_t json_allocation,json_fail_at;
static void *test_json_alloc(size_t bytes) { return ++json_allocation==json_fail_at?NULL:malloc(bytes); }
static reader_vocabulary_record_t rows[4];
static size_t total,count;
static void page(size_t offset) { assert(reader_vocabulary_page(offset,rows,4,&count,&total)); }
static void record(const char *query,const char *book,reader_dict_status_t result) {
    assert(reader_vocabulary_record(query,book,result));
}
int main(void) {
    page(0);assert(!count && !total);
    record("心","书一",READER_DICT_OK);
    record("未见词","书一",READER_DICT_NOT_FOUND);
    record("心","书二",READER_DICT_OK);
    page(0);assert(count==2 && total==2 && !strcmp(rows[0].query,"心") && !strcmp(rows[0].book,"书二") && rows[0].count==2);
    assert(!strcmp(rows[1].query,"未见词") && rows[1].result==READER_DICT_NOT_FOUND && rows[1].count==1);
    int calls=filesystem_calls;usb_owned=true;
    assert(!reader_vocabulary_record("USB","",READER_DICT_OK));
    assert(!reader_vocabulary_page(0,rows,4,&count,&total));assert(filesystem_calls==calls);usb_owned=false;
    card_ready=false;assert(!reader_vocabulary_record("no card","",READER_DICT_OK));
    assert(!reader_vocabulary_page(0,rows,4,&count,&total));assert(filesystem_calls==calls);card_ready=true;
    fail_open=true;assert(!reader_vocabulary_record("readonly","",READER_DICT_OK));fail_open=false;
    fail_sync=true;assert(!reader_vocabulary_record("心","",READER_DICT_OK));fail_sync=false;
    fail_write=true;assert(!reader_vocabulary_record("full","",READER_DICT_OK));fail_write=false;
    page(0);assert(total==2 && rows[0].count==2);
    for(int at=1;at<=2;at++) {
        rename_calls=0;rename_failure=at;
        assert(!reader_vocabulary_record("心","失败写入",READER_DICT_OK));
        page(0);assert(total==2 && rows[0].count==2 && !strcmp(rows[0].book,"书二"));
        rename_failure=0;record("心","书二",READER_DICT_OK);
        page(0);assert(rows[0].count==3);
        // 每次故障测试从同一计数重建。/ Reset the count between independent injected failures.
        FILE *f=fopen(VOCAB_FILE,"wb");assert(f);
        assert(fputs("{\"query\":\"心\",\"book\":\"书二\",\"count\":2,\"result\":0}\n{\"query\":\"未见词\",\"book\":\"书一\",\"count\":1,\"result\":1}\n",f)>=0);fclose(f);
    }
    assert(!rename(VOCAB_FILE,VOCAB_BACKUP));
    FILE *f=fopen(VOCAB_TEMP,"wb");assert(f);fputs("{\"query\":",f);fclose(f);
    page(0);assert(total==2 && rows[0].count==2);
    record("恢复","断电后",READER_DICT_OK);page(0);assert(total==3 && !strcmp(rows[0].query,"恢复"));
    f=fopen(VOCAB_FILE,"ab");assert(f);
    fputs("broken\n",f);for(int i=0;i<10000;i++)fputc('[',f);fputc('\n',f);
    fputs("{\"query\":\"合法\",\"book\":\"书\",\"count\":1,\"result\":0}\n{\"query\":\"断行\"",f);fclose(f);
    page(0);assert(total==4 && !strcmp(rows[3].query,"合法"));
    assert(!reader_vocabulary_record("重建","",READER_DICT_OK));page(0);assert(total==4);
    // 完整坏行不重写；仅尾行截断可丢弃，并保留先前完整记录。/ Refuse complete damaged rows; discard only a truncated tail while keeping complete prior rows.
    f=fopen(VOCAB_FILE,"wb");assert(f);
    fputs("{\"query\":\"合法\",\"book\":\"书\",\"count\":1,\"result\":0}\n{\"query\":\"断行\"",f);fclose(f);
    record("重建","",READER_DICT_OK);page(0);assert(total==2 && !strcmp(rows[0].query,"重建"));
    page(1);assert(count==1 && !strcmp(rows[0].query,"合法"));
    char long_title[400];for(size_t i=0;i<sizeof(long_title)-4;i+=3)memcpy(long_title+i,"书",3);long_title[396]=0;
    record("中文","书名\n\"引号\"\\",READER_DICT_MISSING);page(0);assert(!strcmp(rows[0].book,"书名\n\"引号\"\\") && rows[0].result==READER_DICT_MISSING);
    record("标题",long_title,READER_DICT_OK);page(0);assert(strlen(rows[0].book)==189);
    char long_query[257];memset(long_query,'a',256);long_query[256]=0;
    assert(!reader_vocabulary_record(long_query,"",READER_DICT_OK));
    // 解析分配失败不能被当作损坏行跳过后发布，旧记录与计数必须完整保留。/ Parser allocation failure must never publish a snapshot that drops old rows or resets counts.
    for(size_t failure=1;failure<=120;failure++) {
        f=fopen(VOCAB_FILE,"wb");assert(f);
        fputs("{\"query\":\"old-word\",\"book\":\"Book\",\"count\":7,\"result\":0}\n{\"query\":\"keep-word\",\"book\":\"Book\",\"count\":3,\"result\":0}\n",f);fclose(f);
        json_allocation=0;json_fail_at=failure;
        cJSON_Hooks hooks={.malloc_fn=test_json_alloc,.free_fn=free};cJSON_InitHooks(&hooks);
        bool saved=reader_vocabulary_record("old-word","Updated",READER_DICT_OK);
        cJSON_InitHooks(NULL);
        page(0);
        if(total!=2 || rows[0].count!=(saved?8:7) || rows[1].count!=3)
            fprintf(stderr,"JSON allocation failure at %zu lost a row or count (saved=%d, total=%zu, count=%u)\n",failure,saved,total,rows[0].count);
        assert(total==2 && !strcmp(rows[0].query,"old-word") && rows[0].count==(saved?8:7) && !strcmp(rows[1].query,"keep-word") && rows[1].count==3);
    }
    // 超过一页及长历史仍按最近查询排序，内存不随文件增长。/ Multi-page history remains recent-first without memory proportional to file size.
    for(int i=0;i<1000;i++) { char q[32];snprintf(q,sizeof(q),"word%d",i);record(q,"Book",READER_DICT_OK); }
    record("word0","最新书",READER_DICT_NOT_FOUND);
    page(0);assert(!strcmp(rows[0].query,"word0") && rows[0].count==2 && !strcmp(rows[1].query,"word999"));
    page(998);assert(count==4 && !strcmp(rows[0].query,"word2") && !strcmp(rows[1].query,"word1"));
    puts("notebook 120 allocation-failure points, 1000 records, persistence, paging, coalescing, missing entries, read-only/full disk, USB, truncated rows and rename recovery PASS");
}
