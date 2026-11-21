/* SPDX-License-Identifier: Apache-2.0
 * 复现共用中断被清除后遗留的电源键事件。/ Reproduce a pending key after the shared interrupt is cleared.
 */
#define reader_clock_cancel test_clock_cancel
#define read_pico_ioe_sleep test_ioe_sleep
#define pmu_selftest_prepare_powerdown test_prepare_powerdown
#define read_pico_pmu_power_off test_power_off
#define read_pico_pmu_report_sleep test_report_sleep
#define read_pico_clear_ioe_int test_clear_int
#define read_pico_pmu_take_key_wakeup test_take_key
#define read_pico_pmu_drain_events test_drain
#define gpio_get_level test_gpio_level
#define esp_light_sleep_start test_sleep_start
#include "../../main/sleep.c"
#include <assert.h>

static int clears, sleeps, inject_clear, key_on_sleep;
static bool clock_cancelled = true;
static int clock_cancels, periph_suspends, pmu_prepares, pmu_commands;

bool test_clock_cancel(void) { ++clock_cancels; return clock_cancelled; }
esp_err_t test_ioe_sleep(bool host_off) { (void)host_off; ++periph_suspends; return ESP_OK; }
void test_prepare_powerdown(void) { ++pmu_prepares; }
esp_err_t test_power_off(void) { ++pmu_commands; return ESP_FAIL; }
esp_err_t test_report_sleep(void) { ++pmu_commands; return ESP_FAIL; }
static bool key_pending, irq_low;

void test_clear_int(void) {
    ++clears;
    if (clears == inject_clear) key_pending = true;
    // 读输入端口清中断，但不会替 PMU 确认 FIFO 事件。
    // Reading inputs clears the expander IRQ, not the PMU FIFO.
    irq_low = false;
}
bool test_take_key(void) {
    bool key = key_pending;
    key_pending = false;
    return key;
}
void test_drain(void) { key_pending = false; }
int test_gpio_level(gpio_num_t gpio) { (void)gpio; return irq_low ? 0 : 1; }
esp_err_t test_sleep_start(void) {
    // 有遗留按键却再次进睡，就会等到拔卡等无关边沿才醒。
    // Sleeping with a pending key would wait for an unrelated edge such as card removal.
    assert(!key_pending);
    assert(++sleeps <= 3);
    irq_low = true;
    if (sleeps == key_on_sleep) key_pending = true;
    return ESP_OK;
}

static void run_case(int at_clear, int at_sleep, int expected_sleeps) {
    clears = sleeps = 0;
    key_pending = irq_low = false;
    inject_clear = at_clear;
    key_on_sleep = at_sleep;
    assert(app_light_sleep_wait(NULL) == APP_WAKE_KEY);
    assert(sleeps == expected_sleeps && !key_pending);
}

// 未释放校时网络时，两种底层睡眠入口均不得动外设、PMU或唤醒中断。
// Neither sleep entry may touch peripherals, PMU or wake interrupts before clock cleanup.
static void clock_cancel_gate(void) {
    clock_cancelled = false;
    clears = sleeps = periph_suspends = pmu_prepares = pmu_commands = clock_cancels = 0;
    key_pending = true;
    assert(app_light_sleep_wait(NULL) == APP_WAKE_NONE);
    assert(app_enter_host_sleep(APP_SLEEP_DEEP) == ESP_ERR_INVALID_STATE);
    assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE);
    assert(clock_cancels == 3);
    assert(!clears && !sleeps && !periph_suspends && !pmu_prepares && !pmu_commands);
    assert(key_pending);
    clock_cancelled = true;
    assert(app_enter_host_sleep(APP_SLEEP_DEEP) == ESP_FAIL);
    assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_FAIL);
    assert(periph_suspends == 2 && pmu_prepares == 2 && pmu_commands == 2);
    key_pending = false;
}

int main(void) {
    clock_cancel_gate();
    run_case(2, 0, 0);  // 初次进睡前到键 / key arrives before the first sleep
    run_case(4, 0, 1);  // 无关中断醒来后到键 / key arrives after a spurious wake
    run_case(0, 2, 2);  // 卡边沿不能冒充按键 / a card edge alone must not unlock
    puts("sleep: clock cleanup gate, pending key and unrelated IRQ: PASS");
}
