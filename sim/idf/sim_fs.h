/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 强制包含（-include）到每个固件源文件：把 /sdcard、/flash 开头的路径
 * 重定向到模拟器的数据目录。先把系统头文件包含完，再定义宏。
 *
 * Force-included into every firmware source: redirects paths under /sdcard
 * and /flash into the simulator data directory. System headers come first,
 * then the macros.
 */
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE* sim_fopen(const char* path, const char* mode);
DIR* sim_opendir(const char* path);
int sim_stat(const char* path, struct stat* st);
int sim_lstat(const char* path, struct stat* st);
int sim_mkdir(const char* path, mode_t mode);
int sim_rmdir(const char* path);
int sim_unlink(const char* path);
int sim_remove(const char* path);
int sim_rename(const char* from, const char* to);
int sim_open(const char* path, int flags, ...);
int sim_access(const char* path, int mode);

#ifdef __cplusplus
}
#endif

#ifndef SIM_FS_NO_REDIRECT
#define fopen(p, m) sim_fopen((p), (m))
#define opendir(p) sim_opendir((p))
#define stat(p, s) sim_stat((p), (s))
#define lstat(p, s) sim_lstat((p), (s))
#define mkdir(p, m) sim_mkdir((p), (m))
#define rmdir(p) sim_rmdir((p))
#define unlink(p) sim_unlink((p))
#define remove(p) sim_remove((p))
#define rename(a, b) sim_rename((a), (b))
#define open(...) sim_open(__VA_ARGS__)
#define access(p, m) sim_access((p), (m))
#endif
