// main/power_policy.c —— 省电判定纯逻辑。故意不依赖 ESP-IDF/LVGL，便于 host 测试。
#include "power_policy.h"

uint32_t power_policy_elapsed_ms(uint32_t now_ms, uint32_t since_ms)
{
    return (uint32_t)(now_ms - since_ms);
}

bool power_policy_screen_on(uint32_t now_ms, uint32_t last_input_ms)
{
    return power_policy_elapsed_ms(now_ms, last_input_ms) <=
           POWER_POLICY_SCREEN_OFF_MS;
}

bool power_policy_sleep_due(uint32_t now_ms, uint32_t last_activity_ms)
{
    return power_policy_elapsed_ms(now_ms, last_activity_ms) >=
           POWER_POLICY_SLEEP_IDLE_MS;
}

bool power_policy_can_sleep(bool screen_on, bool ble_connected,
                            bool usb_connected, bool key_held)
{
    return !screen_on && ble_connected && !usb_connected && !key_held;
}
