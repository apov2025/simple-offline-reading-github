/* SPDX-License-Identifier: Apache-2.0
 * 真文件测试配合边界故障注入，验证不丢旧划线。/ Real filesystem tests with boundary failures verify existing highlights survive.
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "read_pico_sd.h"
#include "cJSON.h"
static bool usb_owned,card_ready=true,fail_open,fail_sync,fail_write,fail_read,fail_flush,fail_newline,fail_stat,fail_seek,fail_heap,unmounted;
static int fail_close,close_calls,fail_rename,rename_calls,filesystem_calls,open_handles;
bool reader_usb_active(void) { return usb_owned; }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) {
    *info=(read_pico_sd_info_t){.present=card_ready,.mounted=card_ready && !unmounted};return ESP_OK;
}
static FILE *test_fopen(const char *p,const char *m) {
    ++filesystem_calls;if(fail_open && strchr(m,'w')) { errno=EROFS;return NULL; }FILE *f=fopen(p,m);if(f)++open_handles;return f;
}
static int test_fsync(int fd) { if(fail_sync) { errno=EIO;return -1; }return fsync(fd); }
static int test_fputs(const char *s,FILE *f) { if(fail_write) { errno=ENOSPC;return EOF; }return fputs(s,f); }
static int test_fflush(FILE *f) { return fail_flush?EOF:fflush(f); }
static int test_fputc(int c,FILE *f) { return fail_newline?EOF:fputc(c,f); }
static int test_fstat(int fd,struct stat *st) { return fail_stat?-1:fstat(fd,st); }
static int test_fseek(FILE *f,long offset,int whence) { return fail_seek?-1:fseek(f,offset,whence); }
static void *test_calloc(size_t n,size_t size) { return fail_heap?NULL:calloc(n,size); }
static int test_fgetc(FILE *f) { return fail_read?EOF:fgetc(f); }
static int test_ferror(FILE *f) { return fail_read?1:ferror(f); }
static int test_fclose(FILE *f) { int result=fclose(f);--open_handles;return ++close_calls==fail_close?EOF:result; }
static int test_rename(const char *a,const char *b) {
    if(++rename_calls==fail_rename) { errno=EIO;return -1; }return rename(a,b);
}
#define fflush test_fflush
#define fputc test_fputc
#define fstat test_fstat
#define fseek test_fseek
#define calloc test_calloc
#define fopen test_fopen
#define fsync test_fsync
#define fputs test_fputs
#define fgetc test_fgetc
#define ferror test_ferror
#define fclose test_fclose
#define rename test_rename
#include "../main/annotations/reader_highlight_store.c"
#undef fflush
#undef fputc
#undef fstat
#undef fseek
#undef calloc
#undef fopen
#undef fsync
#undef fputs
#undef fgetc
#undef ferror
#undef fclose
#undef rename
static const char *book="/sdcard/books/a/../书.epub",*body="第一行，hello world.\n下一行。";
static reader_highlight_range_t ranges[8];
static size_t count;
static char file[1024],backup[1030],tempfile_path[1030];
static bool save(size_t start,size_t end) {
    return reader_highlight_save(book,"书名\"\\\n",100,2,"第二章",body,strlen(body),start,end);
}
static void load(size_t want) {
    count=99;assert(reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));assert(count==want);
}
static char *contents(const char *p) {
    FILE *f=fopen(p,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>=0);rewind(f);
    char *s=malloc((size_t)len+1);assert(s);assert(fread(s,1,(size_t)len,f)==(size_t)len);s[len]=0;assert(!fclose(f));return s;
}
static void write_contents(const char *s) {
    FILE *f=fopen(file,"wb");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f));
}
static char *write_json_row(cJSON *json) {
    char *row=cJSON_PrintUnformatted(json);assert(row);
    FILE *f=fopen(file,"wb");assert(f);assert(fputs(row,f)>=0);assert(fputc('\n',f)!=EOF);assert(!fclose(f));
    cJSON_free(row);return contents(file);
}
static void find_file(void) {
    DIR *d=opendir(READER_HIGHLIGHT_DIRECTORY);assert(d);struct dirent *e;int found=0;
    while((e=readdir(d)))if(strstr(e->d_name,".jsonl") && !strstr(e->d_name,".tmp") && !strstr(e->d_name,".bak")) {
        assert(strlen(e->d_name)==22);for(int i=0;i<16;i++)assert(strchr("0123456789abcdef",e->d_name[i]));
        snprintf(file,sizeof(file),"%s/%s",READER_HIGHLIGHT_DIRECTORY,e->d_name);++found;
    }
    closedir(d);assert(found==1);snprintf(backup,sizeof(backup),"%s.bak",file);snprintf(tempfile_path,sizeof(tempfile_path),"%s.tmp",file);
}
static size_t allocation,fail_allocation;
static void *json_alloc(size_t bytes) { return ++allocation==fail_allocation?NULL:malloc(bytes); }
static bool remove_range(size_t start,size_t end) {
    return reader_highlight_remove(book,100,2,body,strlen(body),start,end);
}
static char *current_contents(void) {
    return contents(access(file,F_OK)==0?file:backup);
}
static void assert_current_unchanged(const char *before) {
    char *after=current_contents();assert(!strcmp(before,after));free(after);
}
static void append_contents(const char *line) {
    FILE *f=fopen(file,"ab");assert(f);assert(fputs(line,f)>=0);assert(!fclose(f));
}
static char *range_json(cJSON *json,size_t first,size_t last,const char *quote) {
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"start"),first);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"end"),last);
    assert(cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(json,"text"),quote));
    return write_json_row(json);
}
static void remove_tests(void) {
    const char *old_body=body;body="甲乙 abc def 丁戊";assert(strlen(body)==21);
    write_contents("");assert(reader_highlight_save_at(book,"原书",100,2,"原章",body,21,0,21,1795240800));
    char *seed=contents(file);cJSON *json=cJSON_Parse(seed);free(seed);assert(json);
    cJSON *extension=cJSON_AddObjectToObject(json,"future_metadata");assert(extension);
    assert(cJSON_AddStringToObject(extension,"origin","keep-me"));
    char *full=write_json_row(json);
    // 一个区间必须作用到每条重叠记录，包括重复行；其他章节和旧版本逐字节不变。
    // Apply subtraction to every overlapping row, including duplicates; preserve other chapters/versions byte-for-byte.
    cJSON_DeleteItemFromObjectCaseSensitive(json,"created_at");
    char *inner=range_json(json,3,18,"乙 abc def 丁");
    cJSON_Delete(json);json=cJSON_Parse(full);assert(json);
    char *keep=range_json(json,0,3,"甲");
    cJSON_Delete(json);json=cJSON_Parse(full);assert(json);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"chapter"),9);
    char *other=write_json_row(json);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"chapter"),2);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"file_bytes"),101);
    char *stale=write_json_row(json);cJSON_Delete(json);
    write_contents(full);append_contents(full);append_contents(inner);append_contents(keep);append_contents(other);append_contents(stale);
    assert(remove_range(7,14));load(7);
    const reader_highlight_range_t expected[]={{0,7},{14,21},{0,7},{14,21},{3,7},{14,18},{0,3}};
    for(size_t i=0;i<7;i++)assert(ranges[i].start==expected[i].start && ranges[i].end==expected[i].end);
    char *result=contents(file);assert(strstr(result,other) && strstr(result,stale));
    char *rows=strdup(result),*cursor=NULL;assert(rows);size_t row=0;
    for(char *line=strtok_r(rows,"\n",&cursor);line;line=strtok_r(NULL,"\n",&cursor),row++) {
        json=cJSON_Parse(line);assert(json);
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"title")),"原书"));
        extension=cJSON_GetObjectItemCaseSensitive(json,"future_metadata");
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(extension,"origin")),"keep-me"));
        cJSON *created=cJSON_GetObjectItemCaseSensitive(json,"created_at");
        if(row==4 || row==5)assert(!created);else assert(cJSON_IsString(created) && !strcmp(created->valuestring,"2026-11-21T06:00:00Z"));
        if(row<7) {
            size_t start=expected[row].start,end=expected[row].end;
            const char *quote=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"text"));
            assert(strlen(quote)==end-start && !memcmp(quote,body+start,end-start));
        }
        cJSON_Delete(json);
    }
    assert(row==9);free(rows);
    assert(remove_range(7,14));assert_current_unchanged(result);free(result);
    assert(remove_range(0,21));load(0);result=contents(file);
    assert(strlen(result)==strlen(other)+strlen(stale) && !strncmp(result,other,strlen(other)));
    assert(remove_range(0,21));assert_current_unchanged(result);free(result);
    write_contents(full);assert(remove_range(0,7));load(1);assert(ranges[0].start==7 && ranges[0].end==21);
    write_contents(full);assert(remove_range(14,21));load(1);assert(ranges[0].start==0 && ranges[0].end==14);
    write_contents(full);assert(remove_range(0,21));load(0);
    // 保留空白残片的明确策略：移除文字不会顺便删掉未选择的空白。
    // Explicit whitespace policy: deleting text never removes adjacent unselected whitespace.
    write_contents(full);assert(remove_range(0,6));assert(remove_range(7,21));load(1);
    assert(ranges[0].start==6 && ranges[0].end==7);
    write_contents(full);assert(!remove_range(1,7));assert(!remove_range(0,16));assert(!remove_range(0,0));assert(!remove_range(0,22));
    assert(reader_highlight_remove(book,101,2,body,21,7,14));assert_current_unchanged(full);
    assert(reader_highlight_remove("/missing-highlight-book",100,2,body,21,0,21));
    int calls=filesystem_calls;usb_owned=true;assert(!remove_range(7,14));usb_owned=false;
    unmounted=true;assert(!remove_range(7,14));unmounted=false;assert(filesystem_calls==calls);
    // 写入前后所有可失败边界都必须保留完整旧版。
    // Every failing boundary before publication must preserve the complete old snapshot.
    bool *failures[]={&fail_open,&fail_write,&fail_newline,&fail_flush,&fail_sync,&fail_read,&fail_stat,&fail_seek,&fail_heap};
    for(size_t i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        write_contents(full);*failures[i]=true;assert(!remove_range(7,14));*failures[i]=false;assert_current_unchanged(full);load(1);
    }
    for(int i=1;i<=2;i++) {
        write_contents(full);close_calls=0;fail_close=i;assert(!remove_range(7,14));fail_close=0;assert_current_unchanged(full);load(1);
        rename_calls=0;fail_rename=i;assert(!remove_range(7,14));fail_rename=0;assert_current_unchanged(full);load(1);
        assert(remove_range(7,14));load(2);
    }
    write_contents(full);assert(!rename(file,backup));
    FILE *f=fopen(tempfile_path,"wb");assert(f);fputs("unfinished",f);fclose(f);
    assert(remove_range(7,14));load(2);
    write_contents(full);append_contents("{partial");assert(remove_range(7,14));load(2);
    write_contents(full);append_contents("{}\n");result=contents(file);assert(!remove_range(7,14));assert_current_unchanged(result);free(result);
    for(size_t failure=1;failure<=200;failure++) {
        write_contents(full);allocation=0;fail_allocation=failure;
        cJSON_Hooks hooks={.malloc_fn=json_alloc,.free_fn=free};cJSON_InitHooks(&hooks);
        bool removed=remove_range(7,14);cJSON_InitHooks(NULL);
        if(removed) { load(2);assert(ranges[0].start==0 && ranges[0].end==7 && ranges[1].start==14 && ranges[1].end==21); }
        else { assert_current_unchanged(full);load(1); }
    }
    // 未知元数据的大整数不能经浮点序列化丢精度，嵌套逗号与转义键也须保留。
    // Unknown large integers must not lose precision; nested commas and escaped keys remain valid.
    char *raw=malloc(strlen(full)+128);assert(raw);
    snprintf(raw,strlen(full)+128,"{\"future_integer\":9007199254740993,\"future_array\":[{\"v\":\"a,b}\\\"c\"}],%s",full+1);
    write_contents(raw);assert(remove_range(7,14));load(2);result=contents(file);
    assert(strstr(result,"\"future_integer\":9007199254740993,\"future_array\":[{\"v\":\"a,b}\\\"c\"}]"));free(result);free(raw);
    // 内容变更即使文件大小不变也不删除旧记录。
    // Changed chapter text with the same file size must not remove stale records.
    write_contents(full);assert(reader_highlight_remove(book,100,2,"甲乙 Abc def 丁戊",21,7,14));assert_current_unchanged(full);
    // 拆分上限按最终行数计算，超限不丢任何旧行。
    // Count the final rows after splitting and reject overflow without dropping old rows.
    write_contents("");for(int i=0;i<2049;i++)append_contents(full);
    result=contents(file);assert(!remove_range(7,14));assert_current_unchanged(result);free(result);
    write_contents("");for(int i=0;i<2048;i++)append_contents(full);assert(remove_range(7,14));
    reader_highlight_range_t *all=calloc(4096,sizeof(*all));assert(all);
    assert(reader_highlight_load(book,100,2,body,21,all,4096,&count) && count==4096);free(all);
    // 满行文件仍能缩减；恰好 4 MiB 的有效快照可完整删除。
    // A full-row store can shrink; an exactly 4 MiB valid snapshot can be completely removed.
    f=fopen(file,"wb");assert(f);size_t full_bytes=strlen(full);assert(full_bytes<1024);
    for(int i=0;i<4096;i++) {
        assert(fwrite(full,1,full_bytes-1,f)==full_bytes-1);
        for(size_t j=full_bytes;j<1024;j++)assert(fputc(' ',f)!=EOF);
        assert(fputc('\n',f)!=EOF);
    }
    assert(!fclose(f));struct stat st;assert(!stat(file,&st) && st.st_size==4*1024*1024);
    assert(remove_range(0,21));load(0);assert(!stat(file,&st) && st.st_size==0);
    // 大扩展元数据拆为两份可能超过文件上限，整次拒绝而非截断尾部。
    // Duplicated large extension metadata may exceed the file cap; reject instead of truncating the tail.
    json=cJSON_Parse(full);assert(json);char *padding=malloc(30001);assert(padding);memset(padding,'x',30000);padding[30000]=0;
    assert(cJSON_AddStringToObject(json,"padding",padding));free(padding);char *large=write_json_row(json);cJSON_Delete(json);
    write_contents("");for(int i=0;i<100;i++)append_contents(large);
    result=contents(file);assert(strlen(result)<4*1024*1024);assert(!remove_range(7,14));assert_current_unchanged(result);
    free(result);free(large);free(full);free(inner);free(keep);free(other);free(stale);write_contents("");body=old_body;
    assert(open_handles==0);
}

int main(void) {
    load(0);assert(save(0,12));find_file();load(1);assert(ranges[0].start==0 && ranges[0].end==12);
    char *initial=contents(file);assert(strstr(initial,"第一行，") && strstr(initial,"第二章") && strstr(initial,"书名\\\"\\\\\\n"));
    cJSON *fresh=cJSON_Parse(initial);assert(fresh);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(fresh,"created_at")));cJSON_Delete(fresh);
    assert(save(0,12));char *same=contents(file);assert(!strcmp(initial,same));free(same);
    assert(save(12,23));load(2);assert(ranges[1].start==12 && ranges[1].end==23);
    assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,1,&count) && count==0);
    assert(reader_highlight_load(book,101,2,body,strlen(body),ranges,8,&count) && count==0);
    assert(reader_highlight_load(book,100,3,body,strlen(body),ranges,8,&count) && count==0);
    assert(reader_highlight_load(book,100,2,"第一行，hello World.\n下一行。",strlen(body),ranges,8,&count) && count==0);
    assert(reader_highlight_load("/sdcard/books/other.epub",100,2,body,strlen(body),ranges,8,&count) && count==0);
    assert(!save(1,12));assert(!save(0,2));assert(!save(0,0));assert(!save(0,strlen(body)+1));
    assert(!reader_highlight_save(book,"书",100,2,"章","\xc0\xaf",2,0,2));
    assert(!reader_highlight_save(book,"书",100,2,"章","x\0y",3,0,3));
    int calls=filesystem_calls;usb_owned=true;assert(!save(0,12));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));
    usb_owned=false;card_ready=false;assert(!save(0,12));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));
    card_ready=true;unmounted=true;assert(!save(0,12));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));
    unmounted=false;assert(filesystem_calls==calls);
    write_contents(initial);
    fail_open=true;assert(!save(12,23));fail_open=false;load(1);
    fail_sync=true;assert(!save(12,23));fail_sync=false;load(1);
    fail_write=true;assert(!save(12,23));fail_write=false;load(1);
    fail_flush=true;assert(!save(12,23));fail_flush=false;load(1);
    fail_newline=true;assert(!save(12,23));fail_newline=false;load(1);
    fail_stat=true;assert(!save(12,23));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));fail_stat=false;load(1);
    fail_seek=true;assert(!save(12,23));fail_seek=false;load(1);
    fail_heap=true;assert(!save(12,23));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));fail_heap=false;load(1);
    fail_read=true;assert(!save(12,23));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count));fail_read=false;load(1);
    for(int i=1;i<=2;i++) {
        close_calls=0;fail_close=i;assert(!save(12,23));fail_close=0;load(1);
        rename_calls=0;fail_rename=i;assert(!save(12,23));fail_rename=0;load(1);
        assert(save(12,23));load(2);write_contents(initial);
    }
    assert(!rename(file,backup));FILE *f=fopen(tempfile_path,"wb");assert(f);fputs("{broken",f);fclose(f);
    load(1);assert(save(12,23));load(2);
    write_contents(initial);f=fopen(file,"ab");assert(f);fputs("{\"partial",f);fclose(f);
    load(1);assert(save(12,23));load(2);
    f=fopen(file,"ab");assert(f);fputs("{bad complete}\n",f);fclose(f);
    char *broken=contents(file);assert(!save(24,27));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count) && !count);
    same=contents(file);assert(!strcmp(broken,same));free(same);free(broken);
    // 分配失败不能丢旧行。/ Allocation failures must not discard existing rows.
    for(size_t i=1;i<=200;i++) {
        write_contents(initial);allocation=0;fail_allocation=i;cJSON_Hooks hooks={.malloc_fn=json_alloc,.free_fn=free};cJSON_InitHooks(&hooks);
        bool saved=save(12,23);cJSON_InitHooks(NULL);load(saved?2:1);assert(ranges[0].start==0 && ranges[0].end==12);
    }
    // 大引文与转义扩张不能截断。/ Large quotes and escaping expansion must not truncate.
    char *long_text=malloc(8194);assert(long_text);memset(long_text,'\1',8193);long_text[8193]=0;
    assert(reader_highlight_save(book,"书",100,4,"长章",long_text,8192,0,8192));
    assert(reader_highlight_load(book,100,4,long_text,8192,ranges,8,&count) && count==1 && ranges[0].end==8192);
    assert(!reader_highlight_save(book,"书",100,4,"长章",long_text,8193,0,8193));free(long_text);
    // 满额拒绝新增且保留每一条旧记录；重复仍成功。/ Full stores reject additions, preserve every old row and still accept duplicates.
    f=fopen(file,"wb");assert(f);for(int i=0;i<4096;i++)assert(fputs(initial,f)>=0);fclose(f);
    broken=contents(file);assert(!save(12,23));assert(save(0,12));same=contents(file);assert(!strcmp(broken,same));free(broken);free(same);
    f=fopen(file,"wb");assert(f);assert(!ftruncate(fileno(f),4*1024*1024+1));fclose(f);assert(!save(0,12));
    write_contents(initial);
    // 指纹相同也必须逐字核对引文和路径。/ Even a matching fingerprint must verify quote bytes and book path.
    cJSON *json=cJSON_Parse(initial);assert(json);
    cJSON_ReplaceItemInObjectCaseSensitive(json,"text",cJSON_CreateString("第二行，"));
    char *altered=cJSON_PrintUnformatted(json);assert(altered);f=fopen(file,"wb");assert(f);fputs(altered,f);fputc('\n',f);fclose(f);load(0);cJSON_free(altered);
    cJSON_ReplaceItemInObjectCaseSensitive(json,"text",cJSON_CreateString("第一行，"));
    cJSON_ReplaceItemInObjectCaseSensitive(json,"path",cJSON_CreateString("/other/path"));
    altered=cJSON_PrintUnformatted(json);assert(altered);f=fopen(file,"wb");assert(f);fputs(altered,f);fputc('\n',f);fclose(f);load(0);cJSON_free(altered);cJSON_Delete(json);
    write_contents(initial);
    const char *invalid[]={"{\"text\":\"x\\u0000y\"}\n","{\"text\":{\"nested\":{}}}\n","\n"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        f=fopen(file,"ab");assert(f);fputs(invalid[i],f);fclose(f);
        assert(!save(12,23));assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count) && !count);write_contents(initial);
    }
    char title[514];memset(title,'a',513);title[513]=0;
    assert(!reader_highlight_save(book,title,100,2,"章",body,strlen(body),0,12));
    title[512]=0;assert(reader_highlight_save(book,title,100,2,"章",body,strlen(body),12,23));
    // 老格式与将来的扩展字段均保留；重复保存不得改写创建时间。
    // Preserve legacy and future extension fields; duplicate saves never rewrite creation time.
    json=cJSON_Parse(initial);assert(json);cJSON_DeleteItemFromObjectCaseSensitive(json,"created_at");
    char *legacy=write_json_row(json);load(1);assert(save(0,12));same=contents(file);assert(!strcmp(legacy,same));free(same);
    assert(save(12,23));load(2);same=contents(file);assert(!strncmp(same,legacy,strlen(legacy)));free(same);free(legacy);
    assert(cJSON_AddStringToObject(json,"created_at","2026-11-21T06:00:00Z"));
    cJSON *extension=cJSON_AddObjectToObject(json,"future_metadata");assert(extension);
    assert(cJSON_AddStringToObject(extension,"origin","imported"));
    assert(cJSON_AddNumberToObject(json,"format_revision",2));
    char *extended=write_json_row(json);load(1);assert(save(0,12));same=contents(file);assert(!strcmp(extended,same));free(same);
    assert(save(12,23));load(2);same=contents(file);assert(!strncmp(same,extended,strlen(extended)));free(same);free(extended);
    // 重复的权威字段不能让解析选择歧义，且仍拒绝过深的扩展结构。
    // Reject ambiguous duplicate authoritative fields and excessively deep extensions.
    assert(cJSON_AddStringToObject(json,"path",book));same=write_json_row(json);free(same);
    assert(!reader_highlight_load(book,100,2,body,strlen(body),ranges,8,&count) && !count);assert(!save(12,23));cJSON_Delete(json);
    json=cJSON_Parse(initial);assert(json);extension=cJSON_AddObjectToObject(json,"future");assert(extension);
    for(int i=0;i<5;i++){extension=cJSON_AddObjectToObject(extension,"child");assert(extension);}
    same=write_json_row(json);free(same);assert(!save(12,23));cJSON_Delete(json);
    // 可信 UTC 独立于宿主时区；未可信接口一直写 null，且重复不重置时间。
    // Trusted UTC is independent of host timezone; the untrusted wrapper always writes null and duplicates never reset time.
    const struct { int64_t epoch;const char *utc; } times[]={
        {0,NULL},{1,"1970-01-01T00:00:01Z"},{1795240800,"2026-11-21T06:00:00Z"},
        {951827696,"2000-02-29T12:34:56Z"},{4107542400LL,"2100-03-01T00:00:00Z"},
        {253402300799LL,"9999-12-31T23:59:59Z"}
    };
    assert(!setenv("TZ","Pacific/Honolulu",1));tzset();
    for(size_t i=0;i<sizeof(times)/sizeof(times[0]);i++) {
        write_contents("");assert(reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),0,12,times[i].epoch));
        char *timed=contents(file);json=cJSON_Parse(timed);assert(json);
        cJSON *created=cJSON_GetObjectItemCaseSensitive(json,"created_at");
        if(times[i].utc)assert(cJSON_IsString(created) && !strcmp(created->valuestring,times[i].utc));else assert(cJSON_IsNull(created));
        cJSON_Delete(json);load(1);
        assert(reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),0,12,1795240801));
        assert(save(0,12));same=contents(file);assert(!strcmp(timed,same));free(same);free(timed);
    }
    write_contents(initial);
    assert(!reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),12,23,-1));
    assert(!reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),12,23,253402300800LL));
    assert(!reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),12,23,INT64_MAX));
    same=contents(file);assert(!strcmp(initial,same));free(same);
    assert(reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),0,12,1795240800));
    same=contents(file);assert(!strcmp(initial,same));free(same);
    json=cJSON_Parse(initial);assert(json);cJSON_DeleteItemFromObjectCaseSensitive(json,"created_at");legacy=write_json_row(json);cJSON_Delete(json);
    assert(reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),0,12,1795240800));
    same=contents(file);assert(!strcmp(legacy,same));free(same);free(legacy);
    write_contents(initial);fail_sync=true;
    assert(!reader_highlight_save_at(book,"书",100,2,"章",body,strlen(body),12,23,1795240800));fail_sync=false;
    same=contents(file);assert(!strcmp(initial,same));free(same);
    write_contents(initial);free(initial);assert(open_handles==0);
    remove_tests();
    puts("highlight storage: atomic remove/trim/split/all overlaps, preserved metadata, 200 remove-allocation failures, trusted UTC format/bounds/leap years, created_at null, legacy/extension preservation, timestamp dedup, UTF-8, quote/title escaping, reload/staleness, dedup, overflow, media guards, 200 allocation points, I/O failures, backup recovery, partial tail and bounds PASS");
}
