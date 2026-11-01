// Host test for the pure idle/backlight/sleep policy (no ESP-IDF or LVGL).
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../main/power_policy.h"

static void test_elapsed_wraps(void)
{
    assert(power_policy_elapsed_ms(100, 50) == 50);
    assert(power_policy_elapsed_ms(10, 0xFFFFFFF0u) == 26);
    assert(power_policy_elapsed_ms(0xFFFFFFF0u, 0xFFFFFFF0u) == 0);
}

static void test_screen_boundaries(void)
{
    const uint32_t last = 1000;
    assert(power_policy_screen_on(last, last));
    assert(power_policy_screen_on(last + POWER_POLICY_SCREEN_OFF_MS, last));
    assert(!power_policy_screen_on(last + POWER_POLICY_SCREEN_OFF_MS + 1, last));
    // 回绕边界也要按无符号差值判定。
    const uint32_t wrapped_last = 0xFFFFFF00u;
    assert(power_policy_screen_on(wrapped_last + 100, wrapped_last));
    assert(!power_policy_screen_on(wrapped_last + POWER_POLICY_SCREEN_OFF_MS + 1,
                                   wrapped_last));
}

static void test_sleep_boundaries(void)
{
    const uint32_t last = 500;
    assert(!power_policy_sleep_due(last + POWER_POLICY_SLEEP_IDLE_MS - 1, last));
    assert(power_policy_sleep_due(last + POWER_POLICY_SLEEP_IDLE_MS, last));
    assert(power_policy_sleep_due(last + POWER_POLICY_SLEEP_IDLE_MS + 1, last));
}

static void test_can_sleep_requires_every_condition(void)
{
    assert(power_policy_can_sleep(false, true, false, false));
    assert(!power_policy_can_sleep(true, true, false, false));   // 屏幕亮
    assert(!power_policy_can_sleep(false, false, false, false));  // BLE 未连接
    assert(!power_policy_can_sleep(false, true, true, false));    // USB 插着
    assert(!power_policy_can_sleep(false, true, false, true));    // 按键按住
}

static void test_constants_are_ordered(void)
{
    // 先灭屏再入睡，否则 60 s 的睡眠阈值没有意义。
    assert(POWER_POLICY_SLEEP_IDLE_MS > POWER_POLICY_SCREEN_OFF_MS);
    assert(POWER_POLICY_BACKLIGHT_ON_PERCENT > 0);
    assert(POWER_POLICY_BACKLIGHT_ON_PERCENT <= 100);
}

int main(void)
{
    test_elapsed_wraps();
    test_screen_boundaries();
    test_sleep_boundaries();
    test_can_sleep_requires_every_condition();
    test_constants_are_ordered();
    puts("power_policy: PASS");
    return 0;
}
