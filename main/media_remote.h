// main/media_remote.h —— 蓝牙媒体遥控应用对外接口。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"
#include "esp_err.h"

// 初始化 BLE HID 设备(控制器 + NimBLE + 广播 + HID 报告)。返回 ESP_OK 即已注册设备。
esp_err_t media_remote_init(void);

// 分发一次按键事件(运行于 input 任务)。仅在已连接时发送媒体命令。
void media_remote_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 当前 BLE HID 链路是否已连接（睡眠策略用来判断能否安全进入 light sleep）。
bool media_remote_is_connected(void);

// 背光是否处于点亮状态（由 ui_tick 维护）。
bool media_remote_screen_is_on(void);

// 从 light sleep / 熄屏状态唤醒屏幕：恢复面板扫描、点亮背光并强制刷新 UI。
// 可在非 LVGL 任务调用（不触碰 LVGL 对象）。
void media_remote_wake_screen(void);

// 创建单屏 UI 并启动刷新定时器。必须在 LVGL 任务上持有 bsp_lvgl_lock() 时调用。
void media_remote_ui_build(void);