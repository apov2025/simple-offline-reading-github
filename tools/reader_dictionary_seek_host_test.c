/* SPDX-License-Identifier: Apache-2.0
 * 真实查词实现通过 FILE 替身访问真实 FatFs；虚拟 FAT 只计读取，不预测 SD 耗时。
 * Run the real dictionary through FILE adapters and actual FatFs; count virtual disk reads, never predict SD timing.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ff.h"
#include "diskio.h"
#include "reader_dictionary.h"

PARTITION VolToPart[FF_VOLUMES]={{0,0}};
enum { CLMT_WORDS=256, SECTOR_BYTES=512, CLUSTER_BYTES=4096 };
typedef struct {
    FIL file;
    DWORD map[CLMT_WORDS];
} virtual_file_t;
typedef struct {
    uint64_t fat_reads,data_reads,map_reads;
    unsigned eof_seeks,metadata_reads,opens;
    FRESULT map_result;
    DWORD map_words;
} counters_t;
static FATFS volume;
static virtual_file_t *opened;
static counters_t counters;
static int source_fd;
static uint32_t source_size,cluster_count,stride;
static bool use_fastseek;
static bool fail_read,fail_close,fail_stat;

DSTATUS disk_initialize(BYTE drive) { (void)drive;return 0; }
DSTATUS disk_status(BYTE drive) { (void)drive;return 0; }
DRESULT disk_ioctl(BYTE drive,BYTE command,void *buffer) {
    (void)drive;(void)command;(void)buffer;return RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,LBA_t sector,UINT count) {
    (void)drive;(void)buffer;(void)sector;(void)count;return RES_WRPRT;
}
DRESULT disk_read(BYTE drive,BYTE *buffer,LBA_t sector,UINT count) {
    (void)drive;
    for(UINT i=0;i<count;i++,sector++,buffer+=SECTOR_BYTES) {
        memset(buffer,0,SECTOR_BYTES);
        if(sector>=volume.fatbase && sector<volume.database) {
            ++counters.fat_reads;
            uint32_t first=(uint32_t)(sector-volume.fatbase)*128;
            for(unsigned j=0;j<128;j++) {
                uint32_t cluster=first+j,next=0;
                if(cluster>=2 && (cluster-2)%stride==0) {
                    uint32_t logical=(cluster-2)/stride;
                    if(logical<cluster_count)next=logical+1==cluster_count?0x0fffffff:cluster+stride;
                }
                buffer[j*4]=(BYTE)next;buffer[j*4+1]=(BYTE)(next>>8);
                buffer[j*4+2]=(BYTE)(next>>16);buffer[j*4+3]=(BYTE)(next>>24);
            }
        } else {
            ++counters.data_reads;
            if(sector<volume.database)return RES_PARERR;
            uint64_t relative=sector-volume.database,cluster=relative/volume.csize;
            if(cluster%stride)return RES_PARERR;
            uint64_t offset=(cluster/stride)*CLUSTER_BYTES+(relative%volume.csize)*SECTOR_BYTES;
            if(offset<source_size) {
                size_t bytes=source_size-offset<SECTOR_BYTES?(size_t)(source_size-offset):SECTOR_BYTES;
                if(pread(source_fd,buffer,bytes,(off_t)offset)!=(ssize_t)bytes)return RES_ERROR;
            }
        }
    }
    return RES_OK;
}

// 只替换待测后端的 stdio 符号，测试驱动和 cJSON 仍使用主机标准库。
// Rename stdio only in the backend under test; the driver and cJSON keep the host standard library.
FILE *seek_test_fopen(const char *path,const char *mode) {
    if(strcmp(path,"/virtual/offline.dict")) { errno=ENOENT;return NULL; }
    assert(!opened && !strcmp(mode,"rb"));
    ++counters.opens;
    memset(&volume,0,sizeof(volume));
    volume.fs_type=FS_FAT32;volume.id=1;volume.csize=CLUSTER_BYTES/SECTOR_BYTES;
#if FF_MAX_SS != FF_MIN_SS
    volume.ssize=SECTOR_BYTES;
#endif
    volume.n_fatent=2+cluster_count*stride;
    volume.fatbase=1;volume.fsize=(volume.n_fatent*4+SECTOR_BYTES-1)/SECTOR_BYTES;
    volume.database=volume.fatbase+volume.fsize;volume.winsect=(LBA_t)-1;
    opened=calloc(1,sizeof(*opened));assert(opened);
    opened->file.obj.fs=&volume;opened->file.obj.id=volume.id;
    opened->file.obj.sclust=2;opened->file.obj.objsize=source_size;opened->file.flag=FA_READ;
    if(use_fastseek) {
        opened->map[0]=CLMT_WORDS;opened->file.cltbl=opened->map;
        counters.map_result=f_lseek(&opened->file,CREATE_LINKMAP);
        counters.map_words=opened->map[0];counters.map_reads=counters.fat_reads;
        // 和 ESP-IDF VFS 一样，碎片超出固定表时移除映射并退回普通定位。
        // Match ESP-IDF VFS: remove an insufficient map and fall back to normal seeking.
        if(counters.map_result!=FR_OK)opened->file.cltbl=NULL;
    }
    return (FILE *)opened;
}
int seek_test_fclose(FILE *stream) {
    assert(stream==(FILE *)opened);FRESULT result=f_close(&opened->file);
    free(opened);opened=NULL;return result==FR_OK && !fail_close?0:EOF;
}
int seek_test_fseek(FILE *stream,long offset,int origin) {
    assert(stream==(FILE *)opened);
    int64_t target=offset;
    if(origin==SEEK_END) { ++counters.eof_seeks;target+=source_size; }
    else if(origin==SEEK_CUR)target+=f_tell(&opened->file);
    else assert(origin==SEEK_SET);
    if(target<0 || target>UINT32_MAX) { errno=EINVAL;return -1; }
    return f_lseek(&opened->file,(FSIZE_t)target)==FR_OK?0:-1;
}
long seek_test_ftell(FILE *stream) { assert(stream==(FILE *)opened);return (long)f_tell(&opened->file); }
size_t seek_test_fread(void *buffer,size_t size,size_t count,FILE *stream) {
    assert(stream==(FILE *)opened && (!size || count<=UINT32_MAX/size));
    if(fail_read) { opened->file.err=FR_DISK_ERR;return 0; }
    if(!size)return 0;
    UINT actual=0;
    if(f_read(&opened->file,buffer,(UINT)(size*count),&actual)!=FR_OK)return 0;
    return actual/size;
}
int seek_test_ferror(FILE *stream) { assert(stream==(FILE *)opened);return opened->file.err!=0; }
int seek_test_fileno(FILE *stream) { assert(stream==(FILE *)opened);return 12345; }
int seek_test_fstat(int fd,struct stat *info) {
    assert(fd==12345 && opened);++counters.metadata_reads;
    if(fail_stat) { errno=EIO;return -1; }
    memset(info,0,sizeof(*info));info->st_size=(off_t)f_size(&opened->file);info->st_mode=S_IFREG;return 0;
}

static uint64_t exercise(int scenario,int argc,char **argv) {
    use_fastseek=scenario!=0;stride=scenario==2?257:1;
    uint64_t total_fat_reads=0;
    for(int i=2;i<argc;i+=2) {
        memset(&counters,0,sizeof(counters));
        reader_dict_entry_t entry;
        reader_dict_status_t result=reader_dictionary_lookup_file("/virtual/offline.dict",argv[i],&entry);
        assert(result==READER_DICT_OK && !strcmp(entry.word,argv[i+1]) && entry.count);
        assert(!opened && counters.opens==1);
        if(counters.eof_seeks) {
            fprintf(stderr,"FAIL: lookup '%s' performed %u full EOF seeks before returning; use metadata for length validation\n",argv[i],counters.eof_seeks);
            exit(1);
        }
        assert(counters.metadata_reads>=1);
        if(scenario==1) {
            assert(counters.map_result==FR_OK && counters.map_words==4);
            // 已含建表成本；所有后续索引与正文定位不应再读取 FAT。
            // Include map construction; later index and record seeks must not read FAT again.
            assert(counters.fat_reads==counters.map_reads);
            assert(counters.map_reads<=(cluster_count+2+127)/128);
        } else if(scenario==2) {
            assert(counters.map_result==FR_NOT_ENOUGH_CORE && counters.map_words>CLMT_WORDS);
            assert(counters.fat_reads>counters.map_reads);
        }
        total_fat_reads+=counters.fat_reads;
    }
    printf("%s: queries=%d FAT-sector-reads=%llu\n",scenario==0?"metadata":scenario==1?"metadata+CLMT (including construction)":"fragmented CLMT fallback",(argc-2)/2,(unsigned long long)total_fat_reads);
    return total_fat_reads;
}
static void exercise_group(int argc,char **argv) {
    use_fastseek=true;stride=1;memset(&counters,0,sizeof(counters));
#ifdef READER_DICT_SESSION_API
    reader_dict_session_t session={0};
    assert(reader_dictionary_session_open(&session,"/virtual/offline.dict")==READER_DICT_OK);
#endif
    for(int i=2;i<argc;i+=2) {
        reader_dict_entry_t entry;
#ifdef READER_DICT_SESSION_API
        reader_dict_status_t status=reader_dictionary_session_lookup(&session,argv[i],&entry);
#else
        reader_dict_status_t status=reader_dictionary_lookup_file("/virtual/offline.dict",argv[i],&entry);
#endif
        assert(status==READER_DICT_OK && !strcmp(entry.word,argv[i+1]));
    }
#ifdef READER_DICT_SESSION_API
    reader_dict_entry_t missing;
    assert(reader_dictionary_session_lookup(&session,"no-such-test-key-987654321",&missing)==READER_DICT_NOT_FOUND);
    assert(!missing.count && !missing.word[0]);
    assert(reader_dictionary_session_close(&session)==READER_DICT_OK);
    assert(reader_dictionary_session_close(&session)==READER_DICT_OK);
    assert(reader_dictionary_session_lookup(&session,argv[2],&missing)==READER_DICT_IO_ERROR);
#endif
    if(counters.opens!=1) {
        fprintf(stderr,"FAIL: one query group reopened the same dictionary %u times; expected one open\n",counters.opens);
        exit(1);
    }
    assert(!opened && counters.fat_reads==counters.map_reads);
    printf("grouped queries: one open, FAT-sector-reads=%llu, closed before returning PASS\n",(unsigned long long)counters.fat_reads);
}
static void session_faults(const char *query) {
#ifdef READER_DICT_SESSION_API
    reader_dict_session_t session={0};reader_dict_entry_t entry;
    assert(reader_dictionary_session_open(&session,"/virtual/missing.dict")==READER_DICT_MISSING);
    assert(reader_dictionary_session_lookup(&session,query,&entry)==READER_DICT_MISSING);
    assert(!entry.count && !entry.word[0]);assert(reader_dictionary_session_close(&session)==READER_DICT_OK);
    assert(reader_dictionary_session_open(&session,"/virtual/offline.dict")==READER_DICT_OK);
    assert(reader_dictionary_session_open(&session,"/virtual/offline.dict")==READER_DICT_INVALID);
    fail_read=true;
    assert(reader_dictionary_session_lookup(&session,query,&entry)==READER_DICT_IO_ERROR);
    assert(!entry.count && !entry.word[0]);fail_read=false;
    assert(reader_dictionary_session_lookup(&session,query,&entry)==READER_DICT_IO_ERROR);
    (void)reader_dictionary_session_close(&session);assert(!opened && !session.file);
    assert(reader_dictionary_session_open(&session,"/virtual/offline.dict")==READER_DICT_OK);
    assert(reader_dictionary_session_lookup(&session,query,&entry)==READER_DICT_OK);
    fail_close=true;assert(reader_dictionary_session_close(&session)==READER_DICT_IO_ERROR);
    assert(!opened && !session.file);fail_close=false;
    assert(reader_dictionary_session_open(&session,"/virtual/offline.dict")==READER_DICT_OK);
    fail_stat=true;assert(reader_dictionary_session_lookup(&session,query,&entry)==READER_DICT_IO_ERROR);
    assert(!entry.count && !entry.word[0]);fail_stat=false;
    assert(reader_dictionary_session_close(&session)==READER_DICT_OK && !opened);
    fail_close=true;assert(reader_dictionary_lookup_file("/virtual/offline.dict",query,&entry)==READER_DICT_IO_ERROR);
    assert(!entry.count && !entry.word[0] && !opened);fail_close=false;
    puts("grouped query faults: missing/open/stat/read/close errors release handles and clear failed results PASS");
#endif
}
int main(int argc,char **argv) {
    assert(argc>=4 && !(argc%2));
    source_fd=open(argv[1],O_RDONLY);assert(source_fd>=0);
    struct stat info;assert(fstat(source_fd,&info)==0 && info.st_size>0 && (uint64_t)info.st_size<UINT32_MAX);
    source_size=(uint32_t)info.st_size;cluster_count=(uint32_t)(((uint64_t)source_size+CLUSTER_BYTES-1)/CLUSTER_BYTES);
    assert(cluster_count>127);
    uint64_t regular=exercise(0,argc,argv),fast=exercise(1,argc,argv);
    assert(fast<regular);
    (void)exercise(2,argc,argv);
    exercise_group(argc,argv);
    session_faults(argv[2]);
    reader_dict_entry_t entry;
    assert(reader_dictionary_lookup_file("/virtual/missing.dict","missing",&entry)==READER_DICT_MISSING);
    close(source_fd);
    puts("actual dictionary backend: no EOF traversal, bounded CLMT benefit, fragmented fallback and missing file PASS");
    return 0;
}
