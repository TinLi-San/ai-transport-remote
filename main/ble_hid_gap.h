// main/ble_hid_gap.h —— NimBLE 控制器/主机引导 + HID 可连接广播(媒体遥控的 GAP 层)。
#pragma once

#include "esp_err.h"

// 初始化蓝牙控制器(NimBLE)。必须在其它 ble_hid_gap_* 之前调用。
esp_err_t ble_hid_gap_init(void);

// 配置广播内容(HID service 0x1812 + 设备名)与配对参数。入参 device_name 为常驻字符串。
esp_err_t ble_hid_gap_adv_init(const char *device_name);

// 设置广播字段并开始可连接广播(0 = 永久广播，直到连接或调用停止)。
esp_err_t ble_hid_gap_adv_start(void);

// NimBLE 主循环：交给 esp_nimble_enable()，内部跑 nimble_port_run()。
void ble_hid_host_task(void *param);