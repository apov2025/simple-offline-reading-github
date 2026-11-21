/* SPDX-License-Identifier: Apache-2.0
 * 中文：小纸短时联网校时；主线程管理可信时间，后台仅处理网络。
 * English: Short Pico time-sync sessions; the UI owner manages trusted time, a worker handles networking only.
 * 冻结：不启HTTP，不使用编译日期或NVS旧时间冒充当前日期；PMU操作仅主线程。
 * Frozen: No HTTP, build-date fallback or restoration of stale NVS time; PMU commands stay on the owner thread.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    READER_CLOCK_IDLE, READER_CLOCK_CONNECTING, READER_CLOCK_SYNCING,
    READER_CLOCK_SUCCESS, READER_CLOCK_NO_WIFI, READER_CLOCK_TIMEOUT,
    READER_CLOCK_ERROR, READER_CLOCK_CANCELLED, READER_CLOCK_BUSY,
} reader_clock_state_t;
typedef struct {
    reader_clock_state_t state; ///< 会话状态 / Session status
    bool active, trusted, rtc_retained; ///< 网络未释放、时间可信、PMU已验证持久走时 / Network not yet released, trusted time, verified PMU retention
    int last_error; ///< SDK错误码 / SDK error code
    int timezone_minutes; ///< UTC偏移分钟，默认480 / UTC offset in minutes, default 480
} reader_clock_status_t;

/// 主线程首次进阅读器调用；幂等。读PMU/NVS并决定本次开机是否自动校时，模拟器不联网。
/// Call once on the owner thread after PMU/NVS init; idempotent. Read RTC/provenance and decide boot sync; simulator stays offline.
void reader_clock_init(void);
/// 无联网等待；成功一次时主线程TIME_SYNC/GET有界约60ms，避开并发PMU访问。
/// Never waits for networking; successful completion performs one bounded owner-thread TIME_SYNC/GET (~60ms), avoiding concurrent PMU access.
void reader_clock_tick(void);
/// 主线程启动一次后台校时；忙时拒绝，不启动HTTP。/ Owner starts one background sync; refuses busy sessions and never starts HTTP.
bool reader_clock_request(void);
/// 请求取消并最多等待500ms释放WiFi；false仍清理，必须稍后重试，不能抢占传书或进入睡眠。
/// Request cancellation and join for at most 500ms; false means cleanup continues: retry later, never steal WiFi or sleep yet.
bool reader_clock_cancel(void);
void reader_clock_get_status(reader_clock_status_t *out);
/// 返回当前可信UTC秒；失败不改输出。/ Return current trusted UTC seconds; leave output untouched on failure.
bool reader_clock_trusted_epoch(int64_t *epoch);
/// 输出YYYY-MM-DD、HH:MM；未可信时清空并返回false。/ Format YYYY-MM-DD and HH:MM; clear outputs and return false when untrusted.
bool reader_clock_format(char *date, size_t date_size, char *clock, size_t clock_size);
/// 设置并保存固定UTC偏移，范围-720..840分钟；不隐式联网。/ Set/persist fixed UTC offset (-720..840 minutes), without networking.
bool reader_clock_set_timezone(int minutes);
