// main/hid_consumer.h —— HID Consumer Control(媒体键)报告描述符与发送封装。
#pragma once

#include "esp_hidd.h"
#include "media_cmd.h"

// 单条 consumer report map。media_remote.c 用之构造 esp_hid_device_config_t。
extern esp_hid_raw_report_map_t hid_consumer_report_map;

// 将 media_cmd 编码为一次「按下 + 延迟 + 抬起」的 consumer 输入报告并发送。
// dev 为 esp_hidd_dev_init 返回的设备句柄；未连接或参数无效时静默忽略。
void hid_consumer_send(esp_hidd_dev_t *dev, media_cmd_t cmd);