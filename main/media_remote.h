// main/media_remote.h —— 蓝牙媒体遥控应用对外接口。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// 初始化 BLE HID 设备(控制器 + NimBLE + 广播 + HID 报告)。返回 ESP_OK 即已注册设备。
esp_err_t media_remote_init(void);

// 分发一次按键事件(运行于 input 任务)。仅在已连接时发送媒体命令。
void media_remote_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 创建单屏 UI 并启动刷新定时器。必须在 LVGL 任务上持有 bsp_lvgl_lock() 时调用。
void media_remote_ui_build(void);