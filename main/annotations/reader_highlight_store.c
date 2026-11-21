/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 有界 JSONL 划线存储；临时文件同步后通过备份换名发布。
 * Bounded JSONL highlight storage; sync a temporary snapshot before backup/rename publication.
 * 冻结：只在显式保存或取消划线时写卡；不联网、不重挂载，不访问 USB 独占介质。
 * Frozen: write only on explicit save or removal; no network, remounting or access during USB ownership.
 */
#include "reader_highlight_store.h"
#include "board/reader_usb.h"
#include "read_pico_sd.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef READER_HIGHLIGHT_DIRECTORY
#define READER_HIGHLIGHT_DIRECTORY "/sdcard/notes/highlights"
#endif
enum { MAX_QUOTE=8192, MAX_PATH=1024, MAX_TITLE=512, LINE_BYTES=65536,
       MAX_ROWS=4096, MAX_FILE_BYTES=4*1024*1024 };
typedef struct {
    char current[sizeof(READER_HIGHLIGHT_DIRECTORY)+32];
    char backup[sizeof(READER_HIGHLIGHT_DIRECTORY)+36];
    char temp[sizeof(READER_HIGHLIGHT_DIRECTORY)+36];
} paths_t;
typedef struct {
    cJSON *json;
    const char *path,*text,*fingerprint;
    uint32_t file_bytes,chapter,chapter_bytes,start,end;
} row_t;
typedef struct {
    char line[LINE_BYTES];
    size_t bytes,complete_bytes,rows;
    row_t row;
} workspace_t;

static bool media_ready(void) {
    if(reader_usb_active())return false;
    read_pico_sd_info_t info={0};
    return read_pico_sd_get_info(&info)==ESP_OK && info.present && info.mounted;
}
static bool utf8_valid(const char *s,size_t len) {
    for(size_t i=0;i<len;) {
        unsigned char c=(unsigned char)s[i++];
        if(!c)return false;
        if(c<0x80)continue;
        unsigned extra;uint32_t cp,min;
        if(c>=0xc2 && c<=0xdf) { extra=1;cp=c&31;min=0x80; }
        else if(c>=0xe0 && c<=0xef) { extra=2;cp=c&15;min=0x800; }
        else if(c>=0xf0 && c<=0xf4) { extra=3;cp=c&7;min=0x10000; }
        else return false;
        if(extra>len-i)return false;
        while(extra--) { c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63); }
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    }
    return true;
}
static bool bounded_string(const char *s,size_t limit) {
    return s && strnlen(s,limit+1)<=limit && utf8_valid(s,strlen(s));
}
static bool valid_input(const char *path,size_t chapter,const char *text,size_t len) {
    return bounded_string(path,MAX_PATH) && *path && text && len<=UINT32_MAX &&
        chapter<=UINT32_MAX && utf8_valid(text,len);
}
static bool boundary(const char *text,size_t len,size_t offset) {
    return offset<=len && (offset==len || ((unsigned char)text[offset]&0xc0)!=0x80);
}
static void fingerprint(const char *text,size_t len,char out[17]) {
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<len;i++) { hash^=(unsigned char)text[i];hash*=UINT64_C(1099511628211); }
    snprintf(out,17,"%016" PRIx64,hash);
}
static void make_paths(const char *path,paths_t *paths) {
    char hash[17];fingerprint(path,strlen(path),hash);
    snprintf(paths->current,sizeof(paths->current),"%s/%s.jsonl",READER_HIGHLIGHT_DIRECTORY,hash);
    snprintf(paths->backup,sizeof(paths->backup),"%s.bak",paths->current);
    snprintf(paths->temp,sizeof(paths->temp),"%s.tmp",paths->current);
}
static bool number(cJSON *json,const char *key,uint32_t *value) {
    cJSON *item=cJSON_GetObjectItemCaseSensitive(json,key);
    if(!cJSON_IsNumber(item) || !(item->valuedouble>=0 && item->valuedouble<=UINT32_MAX))return false;
    *value=(uint32_t)item->valuedouble;return (double)*value==item->valuedouble;
}
static bool parse_row(const char *line,row_t *row) {
    // 限制结构深度并拒绝 JSON 内嵌 NUL，避免字符串比较被截断。
    // Bound structural depth and reject embedded JSON NULs so string comparisons cannot truncate.
    unsigned depth=0;bool quoted=false;
    for(const char *p=line;*p;p++) {
        if(quoted && *p=='\\') {
            if(!p[1] || !strncmp(p+1,"u0000",5))return false;
            ++p;continue;
        }
        if(*p=='"')quoted=!quoted;
        else if(!quoted && (*p=='{' || *p=='[')) { if(++depth>4)return false; }
        else if(!quoted && (*p=='}' || *p==']')) { if(!depth)return false;--depth; }
    }
    if(quoted || depth)return false;
    row->json=cJSON_ParseWithOpts(line,NULL,true);
    cJSON *json=row->json;
    if(!cJSON_IsObject(json))return false;
    // 允许旧记录缺少时间与有界扩展，但权威字段不能重名；原始行在重写时原样保留。
    // Accept legacy missing time and bounded extensions, but reject duplicate authoritative keys; rewrites preserve original rows.
    static const char *const keys[]={"path","text","fingerprint","title","chapter_title","file_bytes",
        "chapter","chapter_bytes","start","end","created_at"};
    uint32_t seen=0;
    for(const cJSON *item=json->child;item;item=item->next)for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        if(strcmp(item->string,keys[i]))continue;
        uint32_t bit=UINT32_C(1)<<i;if(seen&bit)return false;seen|=bit;break;
    }
    const cJSON *created=cJSON_GetObjectItemCaseSensitive(json,"created_at");
    if(created && !cJSON_IsNull(created) && (!cJSON_IsString(created) ||
       !bounded_string(created->valuestring,64) || !*created->valuestring))return false;
    row->path=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"path"));
    row->text=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"text"));
    row->fingerprint=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"fingerprint"));
    const char *title=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"title"));
    const char *chapter_title=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"chapter_title"));
    if(!bounded_string(row->path,MAX_PATH) || !*row->path || !bounded_string(row->text,MAX_QUOTE) ||
       !bounded_string(title,MAX_TITLE) || !bounded_string(chapter_title,MAX_TITLE) ||
       !row->fingerprint || strlen(row->fingerprint)!=16 || strspn(row->fingerprint,"0123456789abcdef")!=16 ||
       !number(json,"file_bytes",&row->file_bytes) || !number(json,"chapter",&row->chapter) ||
       !number(json,"chapter_bytes",&row->chapter_bytes) || !number(json,"start",&row->start) || !number(json,"end",&row->end))return false;
    return row->start<row->end && row->end<=row->chapter_bytes && strlen(row->text)==row->end-row->start;
}
// 完整坏行拒绝整个操作；仅忽略没有换行的断写尾部。/ Reject complete malformed rows; ignore only a non-newline-terminated partial tail.
static int next_row(FILE *f,workspace_t *work) {
    cJSON_Delete(work->row.json);memset(&work->row,0,sizeof(work->row));
    size_t n=0;bool bad=false;int ch;
    while((ch=fgetc(f))!=EOF) {
        if(++work->bytes>MAX_FILE_BYTES)return -1;
        if(ch=='\n') {
            work->line[n]=0;
            if(bad || ++work->rows>MAX_ROWS || !parse_row(work->line,&work->row))return -1;
            work->complete_bytes=work->bytes;return 1;
        }
        if(ch==0 || n+1>=sizeof(work->line))bad=true;
        else if(!bad)work->line[n++]=(char)ch;
    }
    return ferror(f)?-1:0;
}
static FILE *open_current(const paths_t *paths,bool *backup,bool *ok) {
    *backup=false;*ok=true;
    FILE *f=fopen(paths->current,"rb");
    if(!f && errno==ENOENT) { f=fopen(paths->backup,"rb");if(f)*backup=true; }
    if(!f) { *ok=errno==ENOENT;return NULL; }
    struct stat st;
    if(fstat(fileno(f),&st)!=0 || st.st_size<0 || st.st_size>MAX_FILE_BYTES) {
        fclose(f);*ok=false;return NULL;
    }
    return f;
}
static bool matches(const row_t *row,const char *path,uint32_t file_bytes,size_t chapter,
                    const char *text,size_t len,const char *hash) {
    return row->file_bytes==file_bytes && row->chapter==chapter && row->chapter_bytes==len &&
        !strcmp(row->path,path) && !strcmp(row->fingerprint,hash) && row->end<=len &&
        boundary(text,len,row->start) && boundary(text,len,row->end) &&
        !memcmp(row->text,text+row->start,row->end-row->start);
}
bool reader_highlight_load(const char *path,uint32_t file_bytes,size_t chapter,const char *text,size_t len,
                           reader_highlight_range_t *out,size_t capacity,size_t *count) {
    if(!count)return false;
    *count=0;
    if((capacity && !out) || !valid_input(path,chapter,text,len) || !media_ready())return false;
    paths_t paths;make_paths(path,&paths);bool backup,ok;
    FILE *source=open_current(&paths,&backup,&ok);
    if(!source)return ok;
    workspace_t *work=calloc(1,sizeof(*work));
    if(!work) { fclose(source);return false; }
    char hash[17];fingerprint(text,len,hash);size_t found=0;int status;
    while((status=next_row(source,work))>0)if(matches(&work->row,path,file_bytes,chapter,text,len,hash)) {
        if(found==capacity) { ok=false;break; }
        out[found++]=(reader_highlight_range_t){work->row.start,work->row.end};
    }
    if(status<0)ok=false;
    if(fclose(source)!=0)ok=false;
    cJSON_Delete(work->row.json);free(work);
    if(ok)*count=found;
    return ok;
}
static char *fresh_line(workspace_t *work,const char *path,const char *title,uint32_t file_bytes,size_t chapter,
                        const char *chapter_title,const char *text,size_t len,size_t start,size_t end,const char *hash,const char *created_at) {
    memcpy(work->line,text+start,end-start);work->line[end-start]=0;
    cJSON *json=cJSON_CreateObject();if(!json)return NULL;
    bool ok=cJSON_AddStringToObject(json,"text",work->line) && cJSON_AddStringToObject(json,"title",title) &&
        cJSON_AddStringToObject(json,"chapter_title",chapter_title) && cJSON_AddNumberToObject(json,"chapter",chapter) &&
        cJSON_AddNumberToObject(json,"start",start) && cJSON_AddNumberToObject(json,"end",end) &&
        cJSON_AddStringToObject(json,"path",path) && cJSON_AddNumberToObject(json,"file_bytes",file_bytes) &&
        cJSON_AddStringToObject(json,"fingerprint",hash) && cJSON_AddNumberToObject(json,"chapter_bytes",len) &&
        // 只格式化调用方明确提供的可信时间；零值保留未知。/ Format only explicitly supplied trusted time; zero stays unknown.
        (*created_at?cJSON_AddStringToObject(json,"created_at",created_at):cJSON_AddNullToObject(json,"created_at"));
    char *line=ok?cJSON_PrintUnformatted(json):NULL;cJSON_Delete(json);return line;
}
static bool make_directory(void) {
    char parent[sizeof(READER_HIGHLIGHT_DIRECTORY)];strcpy(parent,READER_HIGHLIGHT_DIRECTORY);
    char *slash=strrchr(parent,'/');if(!slash || slash==parent)return false;*slash=0;
    if(mkdir(parent,0775)!=0 && errno!=EEXIST)return false;
    return mkdir(READER_HIGHLIGHT_DIRECTORY,0775)==0 || errno==EEXIST;
}
// 独立于时区和 time_t 宽度；转换有明确上界，不读取系统时钟。
// Independent of timezone and time_t width; conversion is bounded and never reads the system clock.
static bool utc_timestamp(int64_t epoch,char out[21]) {
    out[0]=0;
    if(epoch==0)return true;
    if(epoch<0 || epoch>INT64_C(253402300799))return false;
    int days=(int)(epoch/86400),seconds=(int)(epoch%86400),year=1970;
    for(;;year++) {
        int n=365+(year%4==0 && (year%100!=0 || year%400==0));
        if(days<n)break;
        days-=n;
    }
    static const int month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int month=0;
    while(month<11) {
        int n=month_days[month]+(month==1 && year%4==0 && (year%100!=0 || year%400==0));
        if(days<n)break;
        days-=n;month++;
    }
    return snprintf(out,21,"%04d-%02d-%02dT%02d:%02d:%02dZ",year,month+1,days+1,
                    seconds/3600,seconds/60%60,seconds%60)==20;
}
bool reader_highlight_save_at(const char *path,const char *title,uint32_t file_bytes,size_t chapter,
                              const char *chapter_title,const char *text,size_t len,size_t start,size_t end,int64_t trusted_epoch) {
    char created_at[21];if(!utc_timestamp(trusted_epoch,created_at))return false;
    if(!valid_input(path,chapter,text,len) || !bounded_string(title,MAX_TITLE) || !bounded_string(chapter_title,MAX_TITLE) ||
       start>=end || end>len || end-start>MAX_QUOTE || !boundary(text,len,start) || !boundary(text,len,end) || !media_ready())return false;
    workspace_t *work=calloc(1,sizeof(*work));if(!work)return false;
    paths_t paths;make_paths(path,&paths);char hash[17];fingerprint(text,len,hash);
    char *line=fresh_line(work,path,title,file_bytes,chapter,chapter_title,text,len,start,end,hash,created_at);
    bool backup=false,ok=line!=NULL,duplicate=false;FILE *source=NULL,*target=NULL;int status=0;
    if(!ok)goto done;
    source=open_current(&paths,&backup,&ok);if(!ok)goto done;
    while(source && (status=next_row(source,work))>0)
        if(matches(&work->row,path,file_bytes,chapter,text,len,hash) && work->row.start==start && work->row.end==end)duplicate=true;
    if(status<0) { ok=false;goto done; }
    if(duplicate)goto done;
    size_t new_bytes=strlen(line)+1;
    if(work->rows>=MAX_ROWS || new_bytes>=LINE_BYTES || work->complete_bytes>MAX_FILE_BYTES-new_bytes) { ok=false;goto done; }
    if(source && fseek(source,0,SEEK_SET)!=0) { ok=false;goto done; }
    cJSON_Delete(work->row.json);memset(work,0,sizeof(*work));
    if(!make_directory()) { ok=false;goto done; }
    target=fopen(paths.temp,"wb");if(!target) { ok=false;goto done; }
    while(source && (status=next_row(source,work))>0) {
        if(fputs(work->line,target)<0 || fputc('\n',target)==EOF) { ok=false;break; }
    }
    if(status<0)ok=false;
    if(ok)ok=fputs(line,target)>=0 && fputc('\n',target)!=EOF;
    if(ok && fflush(target)!=0)ok=false;
    if(ok && fsync(fileno(target))!=0)ok=false;
    if(fclose(target)!=0)ok=false;
    target=NULL;
    if(source && fclose(source)!=0)ok=false;
    source=NULL;
    if(!ok || !media_ready()) { ok=false;goto done; }
    // 当前文件缺失时只信备份；绝不把未发布的临时文件当成功记录。
    // If current is absent, trust the backup only; never treat an unpublished temp as committed.
    if(!backup) {
        if(unlink(paths.backup)!=0 && errno!=ENOENT) { ok=false;goto done; }
        if(rename(paths.current,paths.backup)!=0 && errno!=ENOENT) { ok=false;goto done; }
    }
    if(rename(paths.temp,paths.current)!=0) { ok=false;goto done; }
    (void)unlink(paths.backup);
done:
    if(target && fclose(target)!=0)ok=false;
    if(source && fclose(source)!=0)ok=false;
    cJSON_Delete(work->row.json);free(work);cJSON_free(line);return ok;
}

bool reader_highlight_save(const char *path,const char *title,uint32_t file_bytes,size_t chapter,
                           const char *chapter_title,const char *text,size_t len,size_t start,size_t end) {
    return reader_highlight_save_at(path,title,file_bytes,chapter,chapter_title,text,len,start,end,0);
}

// 拆分只改范围和原文，保留时间与扩展字段；空白残片也按原字节保留。
// Splits change only range/text, retaining timestamps, extensions and exact whitespace fragments.
static bool write_removed_line(FILE *target,const char *line,size_t *bytes,size_t *rows) {
    size_t n=strlen(line)+1;
    if(n>LINE_BYTES || *rows>=MAX_ROWS || n>MAX_FILE_BYTES-*bytes)return false;
    if(fputs(line,target)<0 || fputc('\n',target)==EOF)return false;
    *bytes+=n;(*rows)++;return true;
}
static const char *skip_json_string(const char *p) {
    ++p;
    while(*p && *p!='"') { if(*p=='\\')++p;++p; }
    return *p?p+1:p;
}
static bool write_fragment(FILE *target,workspace_t *work,const char *text,size_t start,size_t end,
                           size_t *bytes,size_t *rows) {
    char *quote=cJSON_malloc(end-start+1);if(!quote)return false;
    memcpy(quote,text+start,end-start);quote[end-start]=0;
    cJSON *item=cJSON_GetObjectItemCaseSensitive(work->row.json,"text");
    bool ok=cJSON_SetValuestring(item,quote)!=NULL;cJSON_free(quote);
    if(!ok)return false;
    char *escaped=cJSON_PrintUnformatted(item);if(!escaped)return false;
    char first[11],last[11];snprintf(first,sizeof(first),"%u",(unsigned)start);snprintf(last,sizeof(last),"%u",(unsigned)end);
    struct { const char *begin,*end,*value; } replacements[3];size_t found=0;
    const char *p=strchr(work->line,'{')+1;
    // 按已验证对象的字段顺序定位值，只替换三个原始片段；未知大整数等也逐字节保留。
    // Locate values in validated object order and replace only three spans; even unknown large integers remain byte-exact.
    for(const cJSON *child=work->row.json->child;child;child=child->next) {
        p+=strspn(p," \t\r\n,");p=skip_json_string(p);p+=strspn(p," \t\r\n");++p;
        p+=strspn(p," \t\r\n");const char *begin=p;unsigned depth=0;
        while(*p) {
            if(*p=='"') { p=skip_json_string(p);continue; }
            if((*p==',' || *p=='}') && depth==0)break;
            if(*p=='{' || *p=='[')depth++;
            else if(*p=='}' || *p==']')depth--;
            p++;
        }
        const char *value=!strcmp(child->string,"start")?first:!strcmp(child->string,"end")?last:
            !strcmp(child->string,"text")?escaped:NULL;
        if(value) {
            const char *finish=p;while(finish>begin && strchr(" \t\r\n",finish[-1]))finish--;
            replacements[found].begin=begin;replacements[found].end=finish;replacements[found++].value=value;
        }
    }
    size_t n=strlen(work->line);
    for(size_t i=0;i<found;i++)n=n-(size_t)(replacements[i].end-replacements[i].begin)+strlen(replacements[i].value);
    char *line=found==3 && n<LINE_BYTES?cJSON_malloc(n+1):NULL;
    if(!line) { cJSON_free(escaped);return false; }
    char *dest=line;const char *source=work->line;
    for(size_t i=0;i<found;i++) {
        size_t part=(size_t)(replacements[i].begin-source);memcpy(dest,source,part);dest+=part;
        part=strlen(replacements[i].value);memcpy(dest,replacements[i].value,part);dest+=part;source=replacements[i].end;
    }
    strcpy(dest,source);ok=write_removed_line(target,line,bytes,rows);
    cJSON_free(line);cJSON_free(escaped);return ok;
}

bool reader_highlight_remove(const char *path,uint32_t file_bytes,size_t chapter,
                             const char *text,size_t len,size_t start,size_t end) {
    if(!valid_input(path,chapter,text,len) || start>=end || end>len ||
       !boundary(text,len,start) || !boundary(text,len,end) || !media_ready())return false;
    workspace_t *work=calloc(1,sizeof(*work));if(!work)return false;
    paths_t paths;make_paths(path,&paths);char hash[17];fingerprint(text,len,hash);
    bool backup=false,ok=true,changed=false;FILE *target=NULL;
    FILE *source=open_current(&paths,&backup,&ok);int status=0;size_t result_rows=0;
    if(!source)goto done;
    // 先验证全部完整行，再决定最终行数；后续删除可抵消先前拆分增加的行。
    // Validate all complete rows first; later deletions may offset earlier splits in the final row count.
    while((status=next_row(source,work))>0) {
        const row_t *row=&work->row;
        if(matches(row,path,file_bytes,chapter,text,len,hash) && row->start<end && row->end>start) {
            changed=true;result_rows+=(row->start<start)+(row->end>end);
        } else result_rows++;
    }
    if(status<0 || result_rows>MAX_ROWS) { ok=false;goto done; }
    if(!changed)goto done;
    if(fseek(source,0,SEEK_SET)!=0) { ok=false;goto done; }
    cJSON_Delete(work->row.json);memset(work,0,sizeof(*work));
    target=fopen(paths.temp,"wb");if(!target) { ok=false;goto done; }
    size_t output_bytes=0,output_rows=0;
    while((status=next_row(source,work))>0) {
        const row_t *row=&work->row;
        if(matches(row,path,file_bytes,chapter,text,len,hash) && row->start<end && row->end>start) {
            size_t old_start=row->start,old_end=row->end;
            if(old_start<start)ok=write_fragment(target,work,text,old_start,start,&output_bytes,&output_rows);
            if(ok && old_end>end)ok=write_fragment(target,work,text,end,old_end,&output_bytes,&output_rows);
        } else ok=write_removed_line(target,work->line,&output_bytes,&output_rows);
        if(!ok)break;
    }
    if(status<0)ok=false;
    if(ok && fflush(target)!=0)ok=false;
    if(ok && fsync(fileno(target))!=0)ok=false;
    if(fclose(target)!=0)ok=false;
    target=NULL;
    if(fclose(source)!=0)ok=false;
    source=NULL;
    if(!ok || !media_ready()) { ok=false;goto done; }
    // 与保存使用相同发布协议，失败时旧当前文件或备份仍是可信副本。
    // Use the save publication protocol: failure retains the old current or backup as the trusted copy.
    if(!backup) {
        if(unlink(paths.backup)!=0 && errno!=ENOENT) { ok=false;goto done; }
        if(rename(paths.current,paths.backup)!=0 && errno!=ENOENT) { ok=false;goto done; }
    }
    if(rename(paths.temp,paths.current)!=0) { ok=false;goto done; }
    (void)unlink(paths.backup);
done:
    if(target && fclose(target)!=0)ok=false;
    if(source && fclose(source)!=0)ok=false;
    cJSON_Delete(work->row.json);free(work);return ok;
}
