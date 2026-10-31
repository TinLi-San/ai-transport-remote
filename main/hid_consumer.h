// main/hid_consumer.h —— HID Consumer Control(媒体键)报告描述符与发送封装。
#pragma once

#include "esp_hidd.h"
#include "media_cmd.h"

// 单条 consumer report map。media_remote.c 用之构造 esp_hid_device_config_t。
extern esp_hid_raw_report_map_t hid_consumer_report_map;

// report map 里保留了一条厂商自定义输出报告(Report ID 2，160 字节)作为兼容位：
// 本固件不再读取它(旧版曾用来接收电脑下发的会话/直播状态)。保留描述符不动是为了
// 不改 GATT 属性表——改描述符会让 Windows 重新枚举 HID、已配对设备需要重新配对。
// 后续若要彻底删掉它，需同步把 CONFIG_BT_NIMBLE_SVC_HID_MAX_RPTS 从 4 降到 3。

// 将 media_cmd 编码为一次「按下 + 延迟 + 抬起」的 consumer 输入报告并发送。
// dev 为 esp_hidd_dev_init 返回的设备句柄；未连接或参数无效时静默忽略。
void hid_consumer_send(esp_hidd_dev_t *dev, media_cmd_t cmd);
