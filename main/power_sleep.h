// main/power_sleep.h —— 空闲 light sleep 守护任务（显式 esp_light_sleep_start）。
//
// 进入条件见 power_policy_can_sleep()：屏幕已灭、BLE 已连接、USB 未连接、按键已松开，
// 且空闲达到 POWER_POLICY_SLEEP_IDLE_MS。睡眠前暂停 LVGL、交接按键脚，醒来后重建。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// 配置 DFS（max 80 / min 40 MHz，不用自动 light sleep）并启动守护任务。
// btn_cb 是醒来后重建按键驱动时使用的回调（与 bsp_button_init 的同一个）。
// 失败返回错误但不阻塞启动；调用方记录后继续即可。
esp_err_t power_sleep_init(bsp_btn_cb_t btn_cb);

// 任意按键事件调用：重置空闲计时并唤醒守护任务。
// 运行在 button 组件的 esp_timer 任务里，只做原子写与 xTaskNotifyGive。
void power_sleep_activity(void);
