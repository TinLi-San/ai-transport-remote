// main/hid_consumer.c —— HID 报告描述符与媒体命令发送。
//
// 设备同时暴露两个 HID 顶层集合：
//   1) 键盘 (Report ID 1)：短按 ↑/↓ 时发送 →/← 方向键，用于「快进/快退」。
//      Windows 没有 HID 消费类 Fast Forward/Rewind(0xB3/0xB4) 对应的系统媒体键，
//      用方向键才能被浏览器/播放器当作 seek 处理。
//   2) 消费类 (Report ID 3)：播放/暂停、上一/下一、停止等系统媒体键。
//
// 消费类报告描述符移植自 esp-idf 示例 examples/bluetooth/esp_hid_device 的
// mediaReportMap 与 esp_hidd_send_consumer_value()（Apache-2.0 / CC0-1.0），
// 并在按钮数组里追加 Play/Pause(0xCD)。
#include "hid_consumer.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 组合报告描述符：键盘(Report ID 1，含 LED 输出以符合标准键盘) + 消费类(Report ID 3)。
static const uint8_t k_report_map[] = {
    // ===== Keyboard, Report ID 1 (8 字节输入：修饰键 + 保留 + 6 键) =====
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x03,        //   Output (Const)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data,Array)
    0xC0,              // End Collection

    // ===== Consumer Control, Report ID 3 (2 字节输入) =====
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x09, 0x02,        //   Usage (Numeric Key Pad)
    0xA1, 0x02,        //   Collection (Logical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x0A,        //     Usage Maximum (0x0A)
    0x15, 0x01,        //     Logical Minimum (1)
    0x25, 0x0A,        //     Logical Maximum (10)
    0x75, 0x04,        //     Report Size (4)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x00,        //     Input (Data,Array,Abs)
    0xC0,              //   End Collection
    0x05, 0x0C,        //   Usage Page (Consumer)
    0x09, 0x86,        //   Usage (Channel)
    0x15, 0xFF,        //   Logical Minimum (-1)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x02,        //   Report Size (2)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x46,        //   Input (Data,Var,Rel)
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x15, 0x00,        //   Logical Minimum (0)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x09, 0xE2,        //   Usage (Mute)
    0x09, 0x30,        //   Usage (Power)
    0x09, 0x83,        //   Usage (Recall Last)
    0x09, 0x81,        //   Usage (Assign Selection)
    0x09, 0xB0,        //   Usage (Play)
    0x09, 0xB1,        //   Usage (Pause)
    0x09, 0xB2,        //   Usage (Record)
    0x09, 0xB3,        //   Usage (Fast Forward)
    0x09, 0xB4,        //   Usage (Rewind)
    0x09, 0xB5,        //   Usage (Scan Next Track)
    0x09, 0xB6,        //   Usage (Scan Previous Track)
    0x09, 0xB7,        //   Usage (Stop)
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x15, 0x01,        //   Logical Minimum (1)
    0x25, 0x0D,        //   Logical Maximum (13)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0x09, 0x80,        //   Usage (Selection)
    0xA1, 0x02,        //   Collection (Logical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x03,        //     Usage Maximum (0x03)
    0x15, 0x01,        //     Logical Minimum (1)
    0x25, 0x03,        //     Logical Maximum (3)
    0x75, 0x02,        //     Report Size (2)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x00,        //     Input (Data,Array,Abs)
    0xC0,              //   End Collection
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0xC0,              // End Collection
};

esp_hid_raw_report_map_t hid_consumer_report_map = {
    .data = k_report_map,
    .len = sizeof(k_report_map),
};

#define HID_RPT_ID_KB_IN      1   // 键盘输入报告 ID
#define HID_RPT_ID_CC_IN      3   // 消费类输入报告 ID
#define HID_CC_IN_RPT_LEN     2
#define HID_KB_IN_RPT_LEN     8

// 按钮数组索引(与 k_report_map 中 usage 声明顺序一致)。
#define HID_CC_IDX_FAST_FWD   8   // 0xB3
#define HID_CC_IDX_REWIND     9   // 0xB4
#define HID_CC_IDX_NEXT       10  // 0xB5
#define HID_CC_IDX_PREV       11  // 0xB6
#define HID_CC_IDX_STOP       12  // 0xB7
#define HID_CC_IDX_PLAY_PAUSE 13  // 0xCD

// USB HID 键盘用法：方向键
#define HID_KB_ARROW_RIGHT    0x4F
#define HID_KB_ARROW_LEFT     0x50

// 单次触点时长。30ms 足够被主机识别为一次点击。
#define HID_CC_TAP_MS         30
#define HID_KB_TAP_MS         30

static int cmd_to_cc_index(media_cmd_t cmd)
{
    switch (cmd) {
    case MEDIA_CMD_PLAY_PAUSE:    return HID_CC_IDX_PLAY_PAUSE;
    case MEDIA_CMD_NEXT:          return HID_CC_IDX_NEXT;
    case MEDIA_CMD_PREV:          return HID_CC_IDX_PREV;
    case MEDIA_CMD_FAST_FORWARD:  return HID_CC_IDX_FAST_FWD;
    case MEDIA_CMD_REWIND:        return HID_CC_IDX_REWIND;
    case MEDIA_CMD_STOP:          return HID_CC_IDX_STOP;
    default:                      return 0;
    }
}

// 发送一次键盘按键(按下 -> 短延迟 -> 抬起)。用于 ←/→ 方向键 seek。
static void hid_keyboard_tap(esp_hidd_dev_t *dev, uint8_t keycode)
{
    uint8_t buf[HID_KB_IN_RPT_LEN] = {0};
    buf[2] = keycode;                       // key1 = 按键码
    esp_err_t err = esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_KB_IN, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("hid_consumer", "kb press failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("hid_consumer", "TX key=0x%02X", keycode);
    vTaskDelay(pdMS_TO_TICKS(HID_KB_TAP_MS));
    memset(buf, 0, sizeof(buf));            // 抬起
    (void)esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_KB_IN, buf, sizeof(buf));
}

void hid_consumer_send(esp_hidd_dev_t *dev, media_cmd_t cmd)
{
    if (!dev) {
        return;
    }

    // 快进/快退用键盘方向键(Windows 没有可用的系统媒体键)。
    if (cmd == MEDIA_CMD_FAST_FORWARD) {
        hid_keyboard_tap(dev, HID_KB_ARROW_RIGHT);
        return;
    }
    if (cmd == MEDIA_CMD_REWIND) {
        hid_keyboard_tap(dev, HID_KB_ARROW_LEFT);
        return;
    }

    const int idx = cmd_to_cc_index(cmd);
    if (idx == 0) {
        return;
    }

    uint8_t buf[HID_CC_IN_RPT_LEN] = {0, 0};
    buf[1] = (uint8_t)idx;                              // 按下
    esp_err_t err = esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_CC_IN, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("hid_consumer", "press report failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("hid_consumer", "TX cmd=%d idx=%d", (int)cmd, idx);
    vTaskDelay(pdMS_TO_TICKS(HID_CC_TAP_MS));
    buf[1] = 0;                                          // 抬起
    err = esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_CC_IN, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("hid_consumer", "release report failed: %s", esp_err_to_name(err));
    }
}