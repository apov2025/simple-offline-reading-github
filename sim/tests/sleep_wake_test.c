/* SPDX-License-Identifier: Apache-2.0
 * 复现共用中断被清除后遗留的电源键事件。/ Reproduce a pending key after the shared interrupt is cleared.
 */
#define read_pico_clear_ioe_int test_clear_int
#define read_pico_pmu_take_key_wakeup test_take_key
#define read_pico_pmu_drain_events test_drain
#define gpio_get_level test_gpio_level
#define esp_light_sleep_start test_sleep_start
#include "../../main/sleep.c"
#include <assert.h>

static int clears, sleeps, inject_clear, key_on_sleep;
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

int main(void) {
    run_case(2, 0, 0);  // 初次进睡前到键 / key arrives before the first sleep
    run_case(4, 0, 1);  // 无关中断醒来后到键 / key arrives after a spurious wake
    run_case(0, 2, 2);  // 卡边沿不能冒充按键 / a card edge alone must not unlock
    puts("sleep: pending key before/re-entering sleep and unrelated IRQ: PASS");
}
