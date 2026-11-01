// main/power_policy.h —— 盲操遥控器的省电策略常量与纯逻辑（可 host 测试）。
//
// 只放"什么时候灭屏/什么时候该睡"这类可独立验证的判定；真正的 BSP/LVGL/睡眠
// 调用在 media_remote.c 与 power_sleep.c。时间统一用 32-bit 毫秒：49.7 天回绕，
// 差值一律用 uint32_t 无符号减法（见 power_policy_elapsed_ms）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 屏幕：按键后全亮，最后输入后 5 s 全灭；亮度 60% 兼顾盲操可读性与背光电流。
#define POWER_POLICY_SCREEN_OFF_MS          5000u
#define POWER_POLICY_BACKLIGHT_ON_PERCENT   60u

// 整机：屏幕已灭、BLE 已连接、空闲 60 s 后进入 light sleep。60 s 是"看完一段
// 顺手按一下"和"尽快省电"之间的折中；改大只会推迟省电，改小会缩短唤醒键依赖。
#define POWER_POLICY_SLEEP_IDLE_MS          60000u

// 电量计 I2C 读周期。UI tick 仍是 500 ms，避免 2 Hz 读 CW2017。
#define POWER_POLICY_BATTERY_REFRESH_MS     5000u

// 回退开关：真机若发现 light sleep 与 BLE/显示不可兼得，置 0 后只保留
// DFS、面板 DISPOFF、ES8311 suspend 与 T0 改动；重新构建即可，不需要改逻辑。
#define POWER_POLICY_LIGHT_SLEEP_ENABLED    1

// now 与 since 的毫秒差（跨回绕安全）。
uint32_t power_policy_elapsed_ms(uint32_t now_ms, uint32_t since_ms);

// 最后输入之后是否仍应点亮背光。
bool power_policy_screen_on(uint32_t now_ms, uint32_t last_input_ms);

// 空闲是否已满足进入 light sleep 的时长条件。
bool power_policy_sleep_due(uint32_t now_ms, uint32_t last_activity_ms);

// 是否允许进入 light sleep：屏幕已灭、BLE 连接保持、USB 未连接、按键未按住。
bool power_policy_can_sleep(bool screen_on, bool ble_connected,
                            bool usb_connected, bool key_held);
