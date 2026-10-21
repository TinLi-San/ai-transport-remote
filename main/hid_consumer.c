// main/hid_consumer.c —— 移植自 esp-idf 示例
// examples/bluetooth/esp_hid_device/main/esp_hid_device_main.c 的 mediaReportMap
// 与 esp_hidd_send_consumer_value()(Apache-2.0 / CC0-1.0, Espressif Systems)。
// 原示例只把 Play/Pause 拆成 Play(0xB0) 与 Pause(0xB1)；这里在按钮数组里追加
// 复合 usage Play/Pause(0xCD)，并把逻辑最大值从 12 扩到 13，一次按键即可切换。
#include "hid_consumer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Consumer Control 报告描述符，Report ID 3，2 字节输入报告。
// 布局(msb..lsb)：
//   byte0 = [VolUp][VolDn][Channel rel(2)][数字键盘数组(4)]
//   byte1 = [Const 填充(2)][Selection(2)][按钮数组(4)]
// 我们只用 byte1 低半字节的「按钮数组」，索引对应下方 HID_CC_IDX_*。
static const uint8_t k_report_map[] = {
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
    0x81, 0x00,        //     Input (Data,Array,Abs)
    0xC0,              //   End Collection
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0xC0,              // End Collection
};

esp_hid_raw_report_map_t hid_consumer_report_map = {
    .data = k_report_map,
    .len = sizeof(k_report_map),
};

#define HID_RPT_ID_CC_IN      3   // Consumer Control 输入报告 ID
#define HID_CC_IN_RPT_LEN     2

// 按钮数组索引(与 k_report_map 中 usage 声明顺序一致)。
#define HID_CC_IDX_FAST_FWD   8   // 0xB3
#define HID_CC_IDX_REWIND     9   // 0xB4
#define HID_CC_IDX_NEXT       10  // 0xB5
#define HID_CC_IDX_PREV       11  // 0xB6
#define HID_CC_IDX_STOP       12  // 0xB7
#define HID_CC_IDX_PLAY_PAUSE 13  // 0xCD

// 单次触点时长。媒体键为「按键即触发」，按下与抬起之间留很短的间隔即可，
// 过长反而可能被某些播放器解释为按住。30ms 足够注册一次点击。
#define HID_CC_TAP_MS         30

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

void hid_consumer_send(esp_hidd_dev_t *dev, media_cmd_t cmd)
{
    const int idx = cmd_to_cc_index(cmd);
    if (!dev || idx == 0) {
        return;
    }

    uint8_t buf[HID_CC_IN_RPT_LEN] = {0, 0};
    buf[1] = (uint8_t)idx;                              // 按下
    esp_err_t err = esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_CC_IN, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("hid_consumer", "press report failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(HID_CC_TAP_MS));
    buf[1] = 0;                                          // 抬起
    err = esp_hidd_dev_input_set(dev, 0, HID_RPT_ID_CC_IN, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("hid_consumer", "release report failed: %s", esp_err_to_name(err));
    }
}