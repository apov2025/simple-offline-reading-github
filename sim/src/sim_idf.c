/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF 接口在主机上的实现：日志、计时、堆、FreeRTOS（pthread）、NVS（文件）、
 * 文件路径重定向、休眠与重启。
 *
 * Host implementation of the ESP-IDF APIs: log, timer, heap, FreeRTOS on
 * pthreads, NVS on files, path redirection, sleep and restart.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "sim.h"
#include "sim_idf.h"

/* ---- 生命周期 / lifecycle ---- */

static char** s_argv;
static volatile bool s_quit;
static volatile int s_sleep_state;

void sim_set_argv(char** argv) { s_argv = argv; }
void sim_request_quit(void) { s_quit = true; }
bool sim_quit_requested(void) { return s_quit; }
int sim_sleep_state(void) { return s_sleep_state; }

void esp_restart(void) {
    fprintf(stderr, "[sim] esp_restart: re-exec\n");
    fflush(NULL);
    // 告诉重启后的自己"这是重启"，脚本靠它从 rebooted 标记之后接着跑。
    // Tell the restarted process it is a restart; scripts use it to continue after the rebooted marker.
    setenv("SIM_RESTARTED", "1", 1);
    if (s_argv != NULL) execv(s_argv[0], s_argv);
    _exit(0);
}

/* ---- 错误名 / error names ---- */

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
        case ESP_ERR_INVALID_CRC: return "ESP_ERR_INVALID_CRC";
        case ESP_ERR_INVALID_VERSION: return "ESP_ERR_INVALID_VERSION";
        case ESP_ERR_NOT_FINISHED: return "ESP_ERR_NOT_FINISHED";
        case ESP_ERR_NOT_ALLOWED: return "ESP_ERR_NOT_ALLOWED";
        case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
        case ESP_ERR_NVS_INVALID_LENGTH: return "ESP_ERR_NVS_INVALID_LENGTH";
        default: return "ESP_ERR_UNKNOWN";
    }
}

/* ---- 计时 / timer ---- */

static struct timespec s_t0;
static pthread_once_t s_t0_once = PTHREAD_ONCE_INIT;
static void t0_init(void) { clock_gettime(CLOCK_MONOTONIC, &s_t0); }

int64_t esp_timer_get_time(void) {
    pthread_once(&s_t0_once, t0_init);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_sec - s_t0.tv_sec) * 1000000 + (now.tv_nsec - s_t0.tv_nsec) / 1000;
}

void esp_rom_delay_us(uint32_t us) { usleep(us); }

/* ---- 日志 / log ---- */

#define LOG_TAGS_MAX 32
static struct { char tag[24]; esp_log_level_t level; } s_log_tags[LOG_TAGS_MAX];
static int s_log_tag_n;
static pthread_mutex_t s_log_mu = PTHREAD_MUTEX_INITIALIZER;

static esp_log_level_t log_default_level(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* env = getenv("SIM_LOG");
        cached = ESP_LOG_INFO;
        if (env != NULL) {
            if (env[0] == 'e') cached = ESP_LOG_ERROR;
            else if (env[0] == 'w') cached = ESP_LOG_WARN;
            else if (env[0] == 'd') cached = ESP_LOG_DEBUG;
            else if (env[0] == 'v') cached = ESP_LOG_VERBOSE;
            else if (env[0] == 'n') cached = ESP_LOG_NONE;
        }
    }
    return (esp_log_level_t)cached;
}

void esp_log_level_set(const char* tag, esp_log_level_t level) {
    pthread_mutex_lock(&s_log_mu);
    int i;
    for (i = 0; i < s_log_tag_n; i++) {
        if (strcmp(s_log_tags[i].tag, tag) == 0) break;
    }
    if (i == s_log_tag_n && s_log_tag_n < LOG_TAGS_MAX) {
        snprintf(s_log_tags[i].tag, sizeof(s_log_tags[i].tag), "%s", tag);
        s_log_tag_n++;
    }
    if (i < s_log_tag_n) s_log_tags[i].level = level;
    pthread_mutex_unlock(&s_log_mu);
}

esp_log_level_t esp_log_level_get(const char* tag) {
    esp_log_level_t level = log_default_level();
    pthread_mutex_lock(&s_log_mu);
    for (int i = 0; i < s_log_tag_n; i++) {
        if (strcmp(s_log_tags[i].tag, tag) == 0) level = s_log_tags[i].level;
    }
    pthread_mutex_unlock(&s_log_mu);
    return level;
}

void sim_log(esp_log_level_t level, const char* tag, const char* fmt, ...) {
    // 固件用 esp_log_level_set 压低的 tag，在 SIM_LOG=debug 时照样放出来，方便看刷新细节。
    // Tags the firmware quiets via esp_log_level_set still print under SIM_LOG=debug.
    esp_log_level_t limit = log_default_level() >= ESP_LOG_DEBUG ? log_default_level() : esp_log_level_get(tag);
    if (level > limit) return;
    static const char letters[] = "?EWIDV";
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%c (%lld) %s: %s\n", letters[level], (long long)(esp_timer_get_time() / 1000), tag, line);
}

/* ---- 堆 / heap ---- */

void* heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) { (void)caps; return realloc(ptr, size); }
void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)caps;
    void* p = NULL;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (posix_memalign(&p, alignment, size ? size : 1) != 0) return NULL;
    return p;
}
void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps) {
    void* p = heap_caps_aligned_alloc(alignment, n * size, caps);
    if (p != NULL) memset(p, 0, n * size);
    return p;
}
void heap_caps_free(void* ptr) { free(ptr); }
void heap_caps_aligned_free(void* ptr) { free(ptr); }
// 真机 8 MB PSRAM + 512 KB 内部 RAM；这里给一个固定的宽裕值，不模拟内存压力。
// The device has 8 MB PSRAM + 512 KB internal RAM. Fixed, generous numbers; memory pressure is not simulated.
size_t heap_caps_get_free_size(uint32_t caps) { return (caps & MALLOC_CAP_INTERNAL) ? 200 * 1024 : 6 * 1024 * 1024; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { return (caps & MALLOC_CAP_INTERNAL) ? 100 * 1024 : 4 * 1024 * 1024; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
size_t heap_caps_get_total_size(uint32_t caps) { return (caps & MALLOC_CAP_INTERNAL) ? 400 * 1024 : 8 * 1024 * 1024; }
uint32_t esp_get_free_heap_size(void) { return 6 * 1024 * 1024; }
uint32_t esp_random(void) { return (uint32_t)arc4random(); }

/* ---- 系统信息 / system info ---- */

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
    (void)type;
    static const uint8_t fake[6] = { 0x02, 0x00, 0x00, 0x53, 0x49, 0x4D };
    memcpy(mac, fake, 6);
    return ESP_OK;
}
esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_BASE); }

const esp_app_desc_t* esp_app_get_description(void) {
    static esp_app_desc_t desc = {
        .version = "simulator",
        .project_name = "read_pico",
        .time = __TIME__,
        .date = __DATE__,
        .idf_ver = "host",
    };
    return &desc;
}

/* ---- FreeRTOS on pthreads ---- */

struct sim_task {
    pthread_t thread;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    uint32_t notify;
    TaskFunction_t fn;
    void* arg;
};

static pthread_key_t s_task_key;
static pthread_once_t s_task_key_once = PTHREAD_ONCE_INIT;
static void task_key_init(void) { pthread_key_create(&s_task_key, NULL); }

static struct sim_task* task_new(void) {
    struct sim_task* t = calloc(1, sizeof(*t));
    pthread_mutex_init(&t->mu, NULL);
    pthread_cond_init(&t->cv, NULL);
    return t;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    pthread_once(&s_task_key_once, task_key_init);
    struct sim_task* t = pthread_getspecific(s_task_key);
    if (t == NULL) {
        t = task_new();
        t->thread = pthread_self();
        pthread_setspecific(s_task_key, t);
    }
    return t;
}

static void* task_entry(void* arg) {
    struct sim_task* t = arg;
    pthread_once(&s_task_key_once, task_key_init);
    pthread_setspecific(s_task_key, t);
    // 只在 ulTaskNotifyTake 的等待里允许被 vTaskDelete 取消，别处持锁时不会被打断。
    // Cancellation by vTaskDelete is only honored while waiting in ulTaskNotifyTake.
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    t->fn(t->arg);
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
    TaskHandle_t* out, BaseType_t core
) {
    (void)name; (void)stack; (void)prio; (void)core;
    struct sim_task* t = task_new();
    t->fn = fn;
    t->arg = arg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t->thread, &attr, task_entry, t);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(t);
        return pdFAIL;
    }
    if (out != NULL) *out = t;
    return pdPASS;
}

BaseType_t xTaskCreate(
    TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio, TaskHandle_t* out
) {
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, tskNO_AFFINITY);
}

void vTaskDelete(TaskHandle_t task) {
    if (task == NULL || pthread_equal(task->thread, pthread_self())) pthread_exit(NULL);
    pthread_cancel(task->thread);
}

void vTaskDelay(TickType_t ticks) {
    if (ticks == 0) {
        sched_yield();
        return;
    }
    usleep((useconds_t)ticks * 1000);
}

TickType_t xTaskGetTickCount(void) { return (TickType_t)(esp_timer_get_time() / 1000); }

static void deadline_after_ms(struct timespec* ts, uint32_t ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    if (task == NULL) return pdFAIL;
    pthread_mutex_lock(&task->mu);
    task->notify++;
    pthread_cond_signal(&task->cv);
    pthread_mutex_unlock(&task->mu);
    return pdPASS;
}

static void unlock_cleanup(void* mu) { pthread_mutex_unlock(mu); }

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks) {
    struct sim_task* t = xTaskGetCurrentTaskHandle();
    struct timespec deadline;
    if (ticks != portMAX_DELAY) deadline_after_ms(&deadline, ticks);
    int old_state;
    pthread_mutex_lock(&t->mu);
    pthread_cleanup_push(unlock_cleanup, &t->mu);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    while (t->notify == 0) {
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&t->cv, &t->mu);
        } else if (pthread_cond_timedwait(&t->cv, &t->mu, &deadline) == ETIMEDOUT) {
            break;
        }
    }
    pthread_setcancelstate(old_state, NULL);
    pthread_cleanup_pop(0);
    uint32_t value = t->notify;
    if (value > 0) t->notify = clear_on_exit ? 0 : value - 1;
    pthread_mutex_unlock(&t->mu);
    return value;
}

struct sim_sem {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int count;
};

static SemaphoreHandle_t sem_new(int count) {
    struct sim_sem* s = calloc(1, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->count = count;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return sem_new(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return sem_new(0); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    struct timespec deadline;
    if (ticks != portMAX_DELAY) deadline_after_ms(&deadline, ticks);
    BaseType_t ok = pdTRUE;
    pthread_mutex_lock(&sem->mu);
    while (sem->count == 0) {
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&sem->cv, &sem->mu);
        } else if (pthread_cond_timedwait(&sem->cv, &sem->mu, &deadline) == ETIMEDOUT) {
            ok = pdFALSE;
            break;
        }
    }
    if (ok) sem->count--;
    pthread_mutex_unlock(&sem->mu);
    return ok;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    pthread_mutex_lock(&sem->mu);
    sem->count = 1;
    pthread_cond_signal(&sem->cv);
    pthread_mutex_unlock(&sem->mu);
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t sem) {
    if (sem == NULL) return;
    pthread_mutex_destroy(&sem->mu);
    pthread_cond_destroy(&sem->cv);
    free(sem);
}

/* ---- 数据目录与路径重定向 / data directory and path redirection ---- */

static char s_data_dir[PATH_MAX];
static pthread_once_t s_data_once = PTHREAD_ONCE_INIT;

static void mkdir_p(const char* path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void data_dir_init(void) {
    const char* env = getenv("SIM_DATA");
    if (env != NULL && env[0] != 0) {
        if (realpath(env, s_data_dir) == NULL) snprintf(s_data_dir, sizeof(s_data_dir), "%s", env);
    } else {
        // 默认放在可执行文件旁的 ../data，即 sim/data。/ Default: ../data next to the binary, i.e. sim/data.
        char exe[PATH_MAX] = "";
        if (s_argv != NULL && realpath(s_argv[0], exe) != NULL) {
            char* slash = strrchr(exe, '/');
            if (slash != NULL) *slash = 0;
            slash = strrchr(exe, '/');
            if (slash != NULL) *slash = 0;
            snprintf(s_data_dir, sizeof(s_data_dir), "%s/data", exe);
        } else {
            snprintf(s_data_dir, sizeof(s_data_dir), "sim_data");
        }
    }
    char sub[PATH_MAX];
    static const char* subs[] = { "sdcard/books", "sdcard/fonts", "flash", "nvs" };
    for (int i = 0; i < 4; i++) {
        snprintf(sub, sizeof(sub), "%s/%s", s_data_dir, subs[i]);
        mkdir_p(sub);
    }
}

const char* sim_data_dir(void) {
    pthread_once(&s_data_once, data_dir_init);
    return s_data_dir;
}

static bool has_mount_prefix(const char* path, const char* mount) {
    size_t n = strlen(mount);
    return strncmp(path, mount, n) == 0 && (path[n] == 0 || path[n] == '/');
}

const char* sim_map_path(const char* path, char* buf, int buf_len) {
    if (path == NULL) return path;
    if (has_mount_prefix(path, "/sdcard")) {
        // 拔卡后卡上的路径一律不存在。/ With the card removed, nothing under /sdcard exists.
        snprintf(buf, buf_len, "%s/%s%s", sim_data_dir(), sim_sd_mounted() ? "sdcard" : ".no-card", path + 7);
        return buf;
    }
    if (has_mount_prefix(path, "/flash")) {
        snprintf(buf, buf_len, "%s/flash%s", sim_data_dir(), path + 6);
        return buf;
    }
    return path;
}

FILE* sim_fopen(const char* path, const char* mode) {
    char buf[PATH_MAX];
    return fopen(sim_map_path(path, buf, sizeof(buf)), mode);
}
DIR* sim_opendir(const char* path) {
    char buf[PATH_MAX];
    return opendir(sim_map_path(path, buf, sizeof(buf)));
}
int sim_stat(const char* path, struct stat* st) {
    char buf[PATH_MAX];
    return stat(sim_map_path(path, buf, sizeof(buf)), st);
}
int sim_lstat(const char* path, struct stat* st) {
    char buf[PATH_MAX];
    return lstat(sim_map_path(path, buf, sizeof(buf)), st);
}
int sim_mkdir(const char* path, mode_t mode) {
    char buf[PATH_MAX];
    return mkdir(sim_map_path(path, buf, sizeof(buf)), mode);
}
int sim_rmdir(const char* path) {
    char buf[PATH_MAX];
    return rmdir(sim_map_path(path, buf, sizeof(buf)));
}
int sim_unlink(const char* path) {
    char buf[PATH_MAX];
    return unlink(sim_map_path(path, buf, sizeof(buf)));
}
int sim_remove(const char* path) {
    char buf[PATH_MAX];
    return remove(sim_map_path(path, buf, sizeof(buf)));
}
int sim_rename(const char* from, const char* to) {
    char a[PATH_MAX], b[PATH_MAX];
    return rename(sim_map_path(from, a, sizeof(a)), sim_map_path(to, b, sizeof(b)));
}
int sim_open(const char* path, int flags, ...) {
    char buf[PATH_MAX];
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return open(sim_map_path(path, buf, sizeof(buf)), flags, mode);
}
int sim_access(const char* path, int mode) {
    char buf[PATH_MAX];
    return access(sim_map_path(path, buf, sizeof(buf)), mode);
}

/* ---- FAT 挂载 / FAT mount ---- */

esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(
    const char* base_path, const char* partition_label, const esp_vfs_fat_mount_config_t* config,
    wl_handle_t* wl_handle
) {
    (void)base_path; (void)partition_label; (void)config;
    (void)sim_data_dir();
    if (wl_handle != NULL) *wl_handle = 1;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_spiflash_unmount_rw_wl(const char* base_path, wl_handle_t wl_handle) {
    (void)base_path; (void)wl_handle;
    return ESP_OK;
}

static uint64_t dir_bytes(const char* dir) {
    uint64_t total = 0;
    DIR* d = opendir(dir);
    if (d == NULL) return 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += dir_bytes(path);
        else total += (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}

esp_err_t esp_vfs_fat_info(const char* base_path, uint64_t* out_total_bytes, uint64_t* out_free_bytes) {
    char buf[PATH_MAX];
    const bool is_flash = strcmp(base_path, "/flash") == 0;
    if (!is_flash && !sim_sd_mounted()) return ESP_ERR_INVALID_STATE;
    // 容量按真机取：内部存储分区 11 MB，TF 卡按一张 32 GB 卡算。
    // Capacities follow the device: 11 MB internal partition, a 32 GB card.
    const uint64_t total = is_flash ? 11ULL * 1024 * 1024 : 32ULL * 1000 * 1000 * 1000;
    const uint64_t used = dir_bytes(sim_map_path(base_path, buf, sizeof(buf)));
    if (out_total_bytes != NULL) *out_total_bytes = total;
    if (out_free_bytes != NULL) *out_free_bytes = used < total ? total - used : 0;
    return ESP_OK;
}

/* ---- NVS：每个键一个文件 sim/data/nvs/<命名空间>/<键>。/ One file per key. ---- */

#define NVS_HANDLES 16
static struct { bool used; char ns[32]; } s_nvs[NVS_HANDLES];
static pthread_mutex_t s_nvs_mu = PTHREAD_MUTEX_INITIALIZER;

esp_err_t nvs_flash_init(void) { (void)sim_data_dir(); return ESP_OK; }

static void rm_rf(const char* dir) {
    DIR* d = opendir(dir);
    if (d == NULL) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            rm_rf(path);
            rmdir(path);
        } else {
            unlink(path);
        }
    }
    closedir(d);
}

esp_err_t nvs_flash_erase(void) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/nvs", sim_data_dir());
    rm_rf(path);
    return ESP_OK;
}

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/nvs/%s", sim_data_dir(), ns);
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
        mkdir_p(path);
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    pthread_mutex_lock(&s_nvs_mu);
    for (int i = 0; i < NVS_HANDLES; i++) {
        if (!s_nvs[i].used) {
            s_nvs[i].used = true;
            snprintf(s_nvs[i].ns, sizeof(s_nvs[i].ns), "%s", ns);
            *out = (nvs_handle_t)(i + 1);
            err = ESP_OK;
            break;
        }
    }
    pthread_mutex_unlock(&s_nvs_mu);
    return err;
}

void nvs_close(nvs_handle_t h) {
    if (h >= 1 && h <= NVS_HANDLES) s_nvs[h - 1].used = false;
}

esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

static bool nvs_key_path(nvs_handle_t h, const char* key, char* out, size_t out_len) {
    if (h < 1 || h > NVS_HANDLES || !s_nvs[h - 1].used || key == NULL) return false;
    snprintf(out, out_len, "%s/nvs/%s/%s", sim_data_dir(), s_nvs[h - 1].ns, key);
    return true;
}

static esp_err_t nvs_write(nvs_handle_t h, const char* key, const void* data, size_t len) {
    char path[PATH_MAX];
    if (!nvs_key_path(h, key, path, sizeof(path))) return ESP_ERR_NVS_INVALID_HANDLE;
    FILE* f = fopen(path, "wb");
    if (f == NULL) return ESP_FAIL;
    const bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok ? ESP_OK : ESP_FAIL;
}

// out 为 NULL 时只回填长度，与真实 NVS 的"先问长度再读"用法一致。
// With out == NULL only the length is returned, matching the NVS query-then-read idiom.
static esp_err_t nvs_read(nvs_handle_t h, const char* key, void* out, size_t* len) {
    char path[PATH_MAX];
    if (!nvs_key_path(h, key, path, sizeof(path))) return ESP_ERR_NVS_INVALID_HANDLE;
    FILE* f = fopen(path, "rb");
    if (f == NULL) return ESP_ERR_NVS_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    const size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    esp_err_t err = ESP_OK;
    if (out == NULL) {
        *len = size;
    } else if (*len < size) {
        *len = size;
        err = ESP_ERR_NVS_INVALID_LENGTH;
    } else {
        *len = size;
        if (fread(out, 1, size, f) != size) err = ESP_FAIL;
    }
    fclose(f);
    return err;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    char path[PATH_MAX];
    if (!nvs_key_path(h, key, path, sizeof(path))) return ESP_ERR_NVS_INVALID_HANDLE;
    return unlink(path) == 0 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_erase_all(nvs_handle_t h) {
    if (h < 1 || h > NVS_HANDLES || !s_nvs[h - 1].used) return ESP_ERR_NVS_INVALID_HANDLE;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/nvs/%s", sim_data_dir(), s_nvs[h - 1].ns);
    rm_rf(path);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len) {
    return nvs_write(h, key, value, len);
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
    return nvs_read(h, key, out, len);
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value) {
    return nvs_write(h, key, value, strlen(value) + 1);
}
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len) {
    return nvs_read(h, key, out, len);
}

#define SIM_NVS_INT(T, name) \
    esp_err_t nvs_set_##name(nvs_handle_t h, const char* key, T value) { \
        return nvs_write(h, key, &value, sizeof(value)); \
    } \
    esp_err_t nvs_get_##name(nvs_handle_t h, const char* key, T* out) { \
        T value; \
        size_t len = sizeof(value); \
        esp_err_t err = nvs_read(h, key, &value, &len); \
        if (err == ESP_OK && len != sizeof(value)) err = ESP_ERR_NVS_NOT_FOUND; \
        if (err == ESP_OK) *out = value; \
        return err; \
    }
SIM_NVS_INT(uint8_t, u8)
SIM_NVS_INT(int8_t, i8)
SIM_NVS_INT(uint16_t, u16)
SIM_NVS_INT(int16_t, i16)
SIM_NVS_INT(uint32_t, u32)
SIM_NVS_INT(int32_t, i32)
SIM_NVS_INT(uint64_t, u64)
SIM_NVS_INT(int64_t, i64)

/* ---- 休眠 / sleep ---- */

bool sim_take_wake_key(void);  // sim_board.c

esp_err_t sleep_cpu_configure(bool light_sleep_enable) { (void)light_sleep_enable; return ESP_OK; }
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask, esp_sleep_ext1_wakeup_mode_t level_mode) {
    (void)io_mask; (void)level_mode;
    return ESP_OK;
}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_UNDEFINED; }

// 浅睡：停在这里直到电源键（窗口里按 P）。唤醒键事件留给固件自己取走。
// Light sleep: park here until the power key (P in the window). The firmware consumes the wake event itself.
bool sim_wake_key_pending(void);  // sim_board.c
void sim_set_sleep_state(int state) { s_sleep_state = state; }

esp_err_t esp_light_sleep_start(void) {
    s_sleep_state = 1;
    while (!sim_quit_requested() && !sim_wake_key_pending()) usleep(10000);
    s_sleep_state = 0;
    if (sim_quit_requested()) pthread_exit(NULL);
    return ESP_OK;
}

void esp_deep_sleep_start(void) {
    s_sleep_state = 2;
    while (!sim_quit_requested() && !sim_wake_key_pending()) usleep(10000);
    if (sim_quit_requested()) pthread_exit(NULL);
    esp_restart();
}

/* ---- 二维码：传书在模拟器里未实现，生成直接失败。
 * QR code: transfer is not implemented in the simulator; generation fails. ---- */

esp_err_t esp_qrcode_generate(esp_qrcode_config_t* cfg, const char* text) {
    (void)cfg; (void)text;
    return ESP_ERR_NOT_SUPPORTED;
}
int esp_qrcode_get_size(esp_qrcode_handle_t qrcode) { (void)qrcode; return 0; }
bool esp_qrcode_get_module(esp_qrcode_handle_t qrcode, int x, int y) {
    (void)qrcode; (void)x; (void)y;
    return false;
}
