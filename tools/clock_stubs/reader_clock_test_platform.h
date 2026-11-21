#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#define CLOCK_LOCK() ((void)0)
#define CLOCK_UNLOCK() ((void)0)
static int64_t clock_now_ms(void);
static bool clock_load(void *out, size_t size);
static bool clock_save(const void *data, size_t size);
static bool clock_rtc_read(uint32_t *epoch, uint32_t *boot);
static bool clock_rtc_write(uint32_t epoch, uint32_t *boot);
static bool clock_launch(void);
static void clock_pause(void);
static bool clock_usb_busy(void);
