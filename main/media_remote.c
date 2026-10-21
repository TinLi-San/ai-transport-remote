// main/media_remote.c —— 蓝牙媒体遥控：HID 设备 + 连接状态 + 单屏 UI + 按键分发。
#include "media_remote.h"

#include "ble_hid_gap.h"
#include "hid_consumer.h"
#include "media_cmd.h"

#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port_freertos.h"

#include "lvgl.h"

static const char *TAG = "media_remote";

#define DEVICE_NAME "AI Transport Remote"

// 枚举取值一致性：media_cmd.h 与 bsp_button.h 故意对齐，由编译期断言守住。
_Static_assert(BSP_BTN_UP == MEDIA_BTN_UP && BSP_BTN_DOWN == MEDIA_BTN_DOWN &&
               BSP_BTN_OK == MEDIA_BTN_OK, "button enum mismatch");
_Static_assert(BSP_BTN_PRESS == MEDIA_EV_PRESS && BSP_BTN_CLICK == MEDIA_EV_CLICK &&
               BSP_BTN_DOUBLE == MEDIA_EV_DOUBLE && BSP_BTN_LONG == MEDIA_EV_LONG &&
               BSP_BTN_RELEASE == MEDIA_EV_RELEASE, "button event enum mismatch");

// NimBLE 配置存储初始化(sdkconfig 未开 NVS 持久化时为 RAM 存储)。无公开头，向前声明。
void ble_store_config_init(void);

static esp_hidd_dev_t *s_hid_dev;

// 跨任务状态：HID 事件回调写 s_connected，input 任务写 s_action*，LVGL 定时器统读并刷新。
static volatile bool s_connected;
static volatile media_cmd_t s_action = MEDIA_CMD_NONE;
static volatile int64_t s_action_at_us;

static lv_obj_t *s_status_label;
static lv_obj_t *s_action_label;
static lv_obj_t *s_battery_label;

// 省电：无按键 60s 后背光降到 20%，按键恢复 100%。
#define IDLE_DIM_AFTER_US (60LL * 1000 * 1000)
#define DIM_PERCENT       20
#define FULL_PERCENT      100
static volatile int64_t s_last_input_us;

static const char *action_name(media_cmd_t cmd)
{
    switch (cmd) {
    case MEDIA_CMD_PLAY_PAUSE:    return "Play / Pause";
    case MEDIA_CMD_NEXT:          return "Next";
    case MEDIA_CMD_PREV:          return "Previous";
    case MEDIA_CMD_FAST_FORWARD:  return "Fast Forward";
    case MEDIA_CMD_REWIND:        return "Rewind";
    case MEDIA_CMD_STOP:          return "Stop";
    default:                      return "";
    }
}

static void hidd_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        s_connected = false;
        ble_hid_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        s_connected = true;
        ESP_LOGI(TAG, "HID connected");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        ESP_LOGI(TAG, "HID disconnected");
        ble_hid_gap_adv_start();
        break;
    default:
        break;
    }
}

esp_err_t media_remote_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ble_hid_gap_init();
    if (err != ESP_OK) return err;

    err = ble_hid_gap_adv_init(DEVICE_NAME);
    if (err != ESP_OK) return err;

    const esp_hid_device_config_t cfg = {
        .vendor_id = 0x303A,
        .product_id = 0x0001,
        .version = 0x0100,
        .device_name = DEVICE_NAME,
        .manufacturer_name = "FoloToy",
        .serial_number = "AITR-ESP32C3",
        .report_maps = &hid_consumer_report_map,
        .report_maps_len = 1,
    };
    err = esp_hidd_dev_init(&cfg, ESP_HID_TRANSPORT_BLE, hidd_event_cb, &s_hid_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hidd init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    err = esp_nimble_enable(ble_hid_host_task);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble enable failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

void media_remote_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    s_last_input_us = esp_timer_get_time();
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;

    const media_cmd_t cmd = media_cmd_map((int)btn, (int)ev);
    if (cmd == MEDIA_CMD_NONE) return;

    s_action = cmd;
    s_action_at_us = esp_timer_get_time();

    if (s_hid_dev && esp_hidd_dev_connected(s_hid_dev)) {
        hid_consumer_send(s_hid_dev, cmd);
    } else {
        ESP_LOGI(TAG, "not connected, command ignored: %s", action_name(cmd));
    }
}

static void ui_tick(lv_timer_t *t)
{
    (void)t;

    lv_label_set_text(s_status_label, s_connected ? "Connected" : "Not connected");

    const int soc = bsp_battery_soc();
    if (soc < 0) lv_label_set_text(s_battery_label, "--");
    else lv_label_set_text_fmt(s_battery_label, "%d%%", soc);

    const int64_t now = esp_timer_get_time();
    if (s_action != MEDIA_CMD_NONE && (now - s_action_at_us) < 1500000LL) {
        lv_label_set_text(s_action_label, action_name(s_action));
    } else {
        lv_label_set_text(s_action_label, "");
    }

    bsp_display_backlight((now - s_last_input_us) > IDLE_DIM_AFTER_US ? DIM_PERCENT : FULL_PERCENT);
}

void media_remote_ui_build(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101524), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    lv_label_set_text(title, "AI TRANSPORT");

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9FB3C8), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 48);
    lv_label_set_text(subtitle, "Bluetooth Media Remote");

    s_battery_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x9FB3C8), 0);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_label_set_text(s_battery_label, "--");

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x39FF88), 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -34);
    lv_label_set_text(s_status_label, "Not connected");

    s_action_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_action_label, lv_color_hex(0xFFD928), 0);
    lv_obj_align(s_action_label, LV_ALIGN_CENTER, 0, 4);
    lv_label_set_text(s_action_label, "");

    lv_obj_t *legend = lv_label_create(scr);
    lv_obj_set_style_text_color(legend, lv_color_hex(0xC7D3E0), 0);
    lv_obj_set_style_text_align(legend, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(legend, LV_ALIGN_LEFT_MID, 24, 74);
    lv_label_set_text(legend,
        "UP   tap: Fast Forward\n"
        "     hold: Next\n"
        "DOWN tap: Rewind\n"
        "     hold: Previous\n"
        "OK   tap: Play / Pause\n"
        "     hold: Stop");

    s_last_input_us = esp_timer_get_time();
    ui_tick(NULL);                       // 先立即刷新一次，不等首个定时周期
    lv_timer_create(ui_tick, 250, NULL);
    lv_screen_load(scr);
}