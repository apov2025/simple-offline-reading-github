/* SPDX-License-Identifier: Apache-2.0
 * 真实序列化配合隔离 NVS 替身；不访问用户设置。/ Real serialization with isolated NVS substitutes; never access user settings.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "book/book_store.h"
#include "book/book_visibility.h"

typedef struct { char key[16];unsigned char data[512];size_t len; } entry_t;
static entry_t records[16];
static bool namespace_exists;
static esp_err_t open_error,get_error,set_error,erase_error,commit_error;
static unsigned open_handles,writes,erases,commits,opens;
static nvs_open_mode_t active_mode;
static entry_t *find(const char *key) {
    for(size_t i=0;i<16;i++)if(records[i].len && !strcmp(records[i].key,key))return &records[i];
    return NULL;
}
esp_err_t nvs_open(const char *ns,nvs_open_mode_t mode,nvs_handle_t *out) {
    opens++;assert(!strcmp(ns,"rp_hidden"));assert(!open_handles);
    if(open_error)return open_error;
    if(!namespace_exists && mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
    namespace_exists=true;active_mode=mode;*out=1;open_handles++;return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h==1 && open_handles==1);open_handles--; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *len) {
    assert(h==1 && open_handles==1 && strlen(key)<=15);if(get_error)return get_error;
    entry_t *r=find(key);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    if(!out) { *len=r->len;return ESP_OK; }
    if(*len<r->len) { *len=r->len;return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out,r->data,r->len);*len=r->len;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len) {
    assert(h==1 && open_handles==1 && active_mode==NVS_READWRITE && strlen(key)<=15 && len<=512);
    writes++;if(set_error)return set_error;
    entry_t *r=find(key);
    if(!r)for(size_t i=0;i<16;i++)if(!records[i].len) { r=&records[i];break; }
    assert(r);strcpy(r->key,key);memcpy(r->data,data,len);r->len=len;return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==1 && open_handles==1 && active_mode==NVS_READWRITE);erases++;if(erase_error)return erase_error;
    entry_t *r=find(key);if(!r)return ESP_ERR_NVS_NOT_FOUND;r->len=0;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1 && open_handles==1 && active_mode==NVS_READWRITE);commits++;return commit_error;
}
static void expect(const char *path,bool expected) {
    bool hidden=!expected;assert(book_visibility_get(path,&hidden)==ESP_OK && hidden==expected);assert(!open_handles);
}
static void expect_error(const char *path) {
    bool hidden=false;assert(book_visibility_get(path,&hidden)!=ESP_OK && hidden);assert(!open_handles);
}
int main(void) {
    const char *a="/sdcard/books/秘密.epub",*b="/storage/books/other.txt";
    expect(a,false);assert(!namespace_exists && !writes && !erases && !commits);
    assert(book_visibility_set(a,true)==ESP_OK);expect(a,true);expect(b,false);
    unsigned changed=writes+erases+commits;assert(book_visibility_set(a,true)==ESP_OK);assert(changed==writes+erases+commits);
    assert(book_visibility_set(b,true)==ESP_OK);expect(a,true);expect(b,true);
    assert(book_visibility_set(a,false)==ESP_OK);expect(a,false);expect(b,true);
    changed=writes+erases+commits;assert(book_visibility_set(a,false)==ESP_OK);assert(changed==writes+erases+commits);
    // 两个真实 FNV32 碰撞路径不能互相读取为可见、覆盖或恢复。
    // Two actual FNV32-colliding paths must not expose, overwrite or restore each other.
    const char *c="/sdcard/books/1ea116255536553c.epub",*d="/sdcard/books/b0d72baf1153498d.epub";
    assert(book_visibility_set(c,true)==ESP_OK);expect(c,true);expect_error(d);
    entry_t *collision=find("b_38b8958e");assert(collision);entry_t original=*collision;
    assert(book_visibility_set(d,true)!=ESP_OK);assert(book_visibility_set(d,false)!=ESP_OK);
    assert(!memcmp(collision,&original,sizeof(original)));expect(c,true);
    // 真实版本化字节：错误版本、状态、长度、路径终止符均拒绝改变旧记录。
    // Corrupt version/state/length/path terminator bytes must refuse changes to the existing serialized record.
    const size_t offsets[]={0,3,4,5,6,7};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        *collision=original;collision->data[offsets[i]]^=0x7f;entry_t corrupted=*collision;
        expect_error(c);assert(book_visibility_set(c,true)!=ESP_OK);assert(book_visibility_set(c,false)!=ESP_OK);
        assert(!memcmp(collision,&corrupted,sizeof(corrupted)));
    }
    *collision=original;collision->data[collision->len-1]='x';expect_error(c);
    *collision=original;collision->data[9]=0;expect_error(c);
    *collision=original;collision->len=3;expect_error(c);
    *collision=original;collision->len=sizeof(collision->data);expect_error(c);
    *collision=original;collision->data[8]^=1;expect_error(c);assert(book_visibility_set(c,false)!=ESP_OK);*collision=original;
    // 最大有效路径可持久恢复；过长和空参数不触碰 NVS。
    // The longest valid path round-trips; oversized/empty arguments never touch NVS.
    char longest[BOOK_STORE_PATH_MAX+1];memset(longest,'x',sizeof(longest));longest[0]='/';longest[BOOK_STORE_PATH_MAX-1]=0;
    assert(book_visibility_set(longest,true)==ESP_OK);expect(longest,true);assert(book_visibility_set(longest,false)==ESP_OK);expect(longest,false);
    unsigned before=opens;bool hidden=false;
    assert(book_visibility_get(NULL,&hidden)==ESP_ERR_INVALID_ARG && hidden);assert(book_visibility_set(NULL,true)==ESP_ERR_INVALID_ARG);
    assert(book_visibility_get(a,NULL)==ESP_ERR_INVALID_ARG);assert(book_visibility_set("",true)==ESP_ERR_INVALID_ARG);
    longest[BOOK_STORE_PATH_MAX-1]='x';longest[BOOK_STORE_PATH_MAX]=0;
    expect_error(longest);assert(book_visibility_set(longest,true)==ESP_ERR_INVALID_ARG);assert(opens==before);
    // 注入真实边界错误；任何失败不可被返回为可见或成功。
    // Inject boundary failures; no failure may be reported as visible or successful.
    open_error=ESP_FAIL;expect_error(a);assert(book_visibility_set(a,true)==ESP_FAIL);open_error=0;
    get_error=ESP_FAIL;expect_error(c);assert(book_visibility_set(c,false)==ESP_FAIL);get_error=0;expect(c,true);
    set_error=ESP_FAIL;assert(book_visibility_set(a,true)==ESP_FAIL);set_error=0;expect(a,false);
    erase_error=ESP_FAIL;assert(book_visibility_set(c,false)==ESP_FAIL);erase_error=0;expect(c,true);
    commit_error=ESP_FAIL;assert(book_visibility_set(a,true)==ESP_FAIL);commit_error=0;expect(a,true);
    commit_error=ESP_FAIL;assert(book_visibility_set(a,false)==ESP_FAIL);commit_error=0;expect(a,false);
    assert(!open_handles);expect(b,true);expect(c,true);
    puts("book visibility: isolated namespace, hide/restore, serialization, true hash collision, corruption, maximum path, fail-closed reads, NVS errors and commit-error propagation PASS");
}
