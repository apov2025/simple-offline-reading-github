// 验证实际 BMP 方向、独占命名和失败清理。/ Verify real BMP orientation, exclusive naming and failure cleanup.
#include "reader_screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

// 限制子进程的实际文件长度，覆盖缓冲关闭失败与中途写入失败。/ Limit child file size to cover buffered-close and mid-write failures.
static void full_output_test(const char* dir,int width,int height,const char* stem) {
    pid_t child=fork();assert(child>=0);
    if(child==0) {
        signal(SIGXFSZ,SIG_IGN);
        struct rlimit limit={64,64};assert(setrlimit(RLIMIT_FSIZE,&limit)==0);
        uint8_t fb[8192]={0};char path[512];
        assert(!reader_screenshot_write(dir,stem,fb,width,height,path,sizeof(path)) && !path[0]);
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    char expected[512];snprintf(expected,sizeof(expected),"%s/%s-0000.bmp",dir,stem);
    assert(access(expected,F_OK)!=0 && errno==ENOENT);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const uint8_t fb[] = {0x21, 0x43, 0x65, 0x87};
    char a[512], b[512];
    assert(reader_screenshot_write(argv[1], "a/b", fb, 4, 2, a, sizeof(a)));
    assert(reader_screenshot_write(argv[1], "a/b", fb, 4, 2, b, sizeof(b)));
    assert(strcmp(a,b) != 0);
    FILE* f = fopen(a, "rb"); assert(f);
    unsigned char out[134]; assert(fread(out,1,sizeof(out),f)==134); fclose(f);
    assert(out[0]=='B' && out[1]=='M' && out[18]==2 && out[22]==4 && out[28]==4);
    // 逻辑下行 8/4，然后7/3、6/2、5/1。/ Logical rows bottom-up: 8/4, 7/3, 6/2, 5/1.
    assert(out[118]==0x84 && out[122]==0x73 && out[126]==0x62 && out[130]==0x51);
    assert(out[119]==0 && out[120]==0 && out[121]==0);
    assert(!reader_screenshot_write(a,"file-is-not-a-dir",fb,4,2,b,sizeof(b)) && !b[0]);
    assert(!reader_screenshot_write(argv[1],"bad",fb,3,2,b,sizeof(b)));
    char tiny[2]; assert(!reader_screenshot_write(argv[1],"long",fb,4,2,tiny,sizeof(tiny)));
    full_output_test(argv[1],4,2,"close-failed");
    full_output_test(argv[1],128,128,"write-failed");
    puts("screenshot orientation/unique names/invalid output/partial-file cleanup: PASS");
}
