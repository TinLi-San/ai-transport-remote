// main/media_remote.c —— 蓝牙媒体遥控：HID 设备 + 连接状态 + 盲操单屏 UI + 按键分发。
//
// 产品定位是「盲操的便携式手持遥控器」：屏幕只做手感参考，显示设备名、电量、连接
// 状态、四行按键图例和短暂的动作名；不显示任何播客内容(标题/艺人/进度/时长)。
// 设备不需要知道电脑在播什么：键位只依赖按键本身，配对即可用，电脑上不需要装任何程序。
#include "media_remote.h"

#include <stdbool.h>

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
#include "services/gap/ble_svc_gap.h"

#include "lvgl.h"

static const char *TAG = "media_remote";

// 名称同时进入广播包(31 字节上限)与 GATT 设备名；保持较短以留出广播余量。
#define DEVICE_NAME "AI Transport"

// 枚举取值一致性：media_cmd.h 与 bsp_button.h 故意对齐，由编译期断言守住。
// (强转 int 以避免 -Wenum-compare 把两个枚举间的比较当成错误。)
_Static_assert((int)BSP_BTN_UP == (int)MEDIA_BTN_UP &&
               (int)BSP_BTN_DOWN == (int)MEDIA_BTN_DOWN &&
               (int)BSP_BTN_OK == (int)MEDIA_BTN_OK, "button enum mismatch");
_Static_assert((int)BSP_BTN_PRESS == (int)MEDIA_EV_PRESS &&
               (int)BSP_BTN_CLICK == (int)MEDIA_EV_CLICK &&
               (int)BSP_BTN_DOUBLE == (int)MEDIA_EV_DOUBLE &&
               (int)BSP_BTN_LONG == (int)MEDIA_EV_LONG &&
               (int)BSP_BTN_RELEASE == (int)MEDIA_EV_RELEASE &&
               (int)BSP_BTN_CHORD == (int)MEDIA_EV_CHORD, "button event enum mismatch");

// NimBLE 配置存储初始化(sdkconfig 未开 NVS 持久化时为 RAM 存储)。无公开头，向前声明。
void ble_store_config_init(void);

static esp_hidd_dev_t *s_hid_dev;

// 跨任务状态：HID 事件回调写 s_connected，input 任务写 s_action*，LVGL 定时器统读并刷新。
static volatile bool s_connected;
static volatile media_cmd_t s_action = MEDIA_CMD_NONE;
static volatile int64_t s_action_at_us;
static volatile bool s_ui_dirty = true;   // 按键/背光点亮时置位：强制重刷全部文本

static lv_obj_t *s_status_label;
static lv_obj_t *s_action_label;
static lv_obj_t *s_battery_label;

// 省电：盲操时不看屏，无按键 10 s 后背光全灭，任意按键立即点亮。
// (旧版是 60 s 后降到 20% 且永不熄灭——背光是整机最大的耗电项之一。)
#define IDLE_OFF_AFTER_US (10LL * 1000 * 1000)
#define FULL_PERCENT      100
#define OFF_PERCENT       0
static volatile int64_t s_last_input_us;
static int s_last_hid_battery = -1;   // 最近一次同步给 HID 电量服务的 SOC

static const char *action_name(media_cmd_t cmd)
{
    switch (cmd) {
    case MEDIA_CMD_PLAY_PAUSE:    return "Play / Pause";
    case MEDIA_CMD_NEXT:          return "Next";
    case MEDIA_CMD_PREV:          return "Previous";
    case MEDIA_CMD_FAST_FORWARD:  return "Fast Forward";
    case MEDIA_CMD_REWIND:        return "Rewind";
    case MEDIA_CMD_FULLSCREEN:    return "Fullscreen";
    case MEDIA_CMD_VOLUME_UP:     return "Volume +";
    case MEDIA_CMD_VOLUME_DOWN:   return "Volume -";
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

    // esp_hid 只把设备名用于广播；GAP 服务的 Device Name 仍是 NimBLE 默认的
    // "nimble"，这里补设一次，避免配对后系统把设备显示成 nimble。
    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    err = esp_nimble_enable(ble_hid_host_task);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble enable failed: %s", esp_err_to_name(err));
        return err;
    }

    // 属性表已就绪。
    return ESP_OK;
}

static bool hid_ready(void)
{
    return s_hid_dev && esp_hidd_dev_connected(s_hid_dev);
}

void media_remote_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    s_last_input_us = esp_timer_get_time();
    s_ui_dirty = true;
    // 背光立刻点亮，不等 500 ms 的 UI 定时器：按键反馈应当是即时的。
    bsp_display_backlight(FULL_PERCENT);

    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG && ev != BSP_BTN_CHORD) return;

    const media_cmd_t cmd = media_cmd_map((int)btn, (int)ev);
    // 一行一个按键事件的诊断日志：排查「事件没到」与「发了但主机不认」时最先看它。
    ESP_LOGI(TAG, "key btn=%d ev=%d cmd=%d", (int)btn, (int)ev, (int)cmd);
    if (cmd == MEDIA_CMD_NONE) return;

    s_action = cmd;
    s_action_at_us = esp_timer_get_time();

    if (hid_ready()) {
        hid_consumer_send(s_hid_dev, cmd);
    } else {
        ESP_LOGI(TAG, "not connected, command ignored: %s", action_name(cmd));
    }
}

// LVGL 上下文的周期性刷新。只在内容真的变化时才写标签，避免屏幕和 SPI 一直工作。
// 背光熄灭期间不碰 LVGL；点亮时通过 s_ui_dirty 把全部文本重写一遍。
static void ui_tick(lv_timer_t *t)
{
    (void)t;
    const int64_t now = esp_timer_get_time();

    // 背光策略：按键后 10 s 内全亮，之后全灭。盲操不需要常亮。
    const bool screen_on = (now - s_last_input_us) <= IDLE_OFF_AFTER_US;
    bsp_display_backlight(screen_on ? FULL_PERCENT : OFF_PERCENT);

    // 电量同步给 HID Battery Service 与背光无关，熄灭期间也要继续(否则连接后
    // 电量永远不更新：未连接时 esp_hidd_dev_battery_set 会静默丢弃值)。
    const int soc = bsp_battery_soc();
    if (soc >= 0 && s_hid_dev && soc != s_last_hid_battery &&
        esp_hidd_dev_connected(s_hid_dev)) {
        esp_hidd_dev_battery_set(s_hid_dev, (uint8_t)soc);
        s_last_hid_battery = soc;
    }

    if (!screen_on) return;

    static int s_shown_connected = -1;
    static int s_shown_soc = -2;
    static media_cmd_t s_shown_action = MEDIA_CMD_NONE;
    const bool dirty = s_ui_dirty;

    if (dirty || s_shown_connected != (int)s_connected) {
        lv_label_set_text(s_status_label, s_connected ? "Connected" : "Not connected");
        lv_obj_set_style_text_color(s_status_label,
                                    lv_color_hex(s_connected ? 0x39FF88 : 0xFFB020), 0);
        s_shown_connected = (int)s_connected;
    }

    if (dirty || s_shown_soc != soc) {
        if (soc < 0) lv_label_set_text(s_battery_label, "--");
        else lv_label_set_text_fmt(s_battery_label, "%d%%", soc);
        s_shown_soc = soc;
    }

    if (s_action != MEDIA_CMD_NONE && (now - s_action_at_us) < 1500000LL) {
        if (dirty || s_shown_action != s_action) {
            lv_label_set_text(s_action_label, action_name(s_action));
            s_shown_action = s_action;
        }
    } else if (s_shown_action != MEDIA_CMD_NONE) {
        lv_label_set_text(s_action_label, "");
        s_shown_action = MEDIA_CMD_NONE;
    }

    s_ui_dirty = false;
}

// 一行图例：左侧按键图标，中间短按动作，右侧长按动作。
static void add_guide_row(lv_obj_t *scr, int y, const char *key_sym,
                          const char *tap_text, const char *hold_text)
{
    lv_obj_t *k = lv_label_create(scr);
    lv_obj_set_style_text_color(k, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 22, y);
    lv_label_set_text(k, key_sym);

    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8F0F7), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 62, y);
    lv_label_set_text(t, tap_text);

    lv_obj_t *h = lv_label_create(scr);
    lv_obj_set_style_text_color(h, lv_color_hex(0x9FB3C8), 0);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 148, y);
    lv_label_set_text(h, hold_text);
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(title, "AI TRANSPORT");

    s_battery_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x9FB3C8), 0);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(s_battery_label, "--");

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x39FF88), 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(s_status_label, "Not connected");

    // 四行按键图例：↑ / ↓ / OK 的短按与长按（图标 + 极少文字），外加 ⏎+↑/↓ 的音量组合键。
    // 屏幕不承载任何播客内容，图例就是这块屏的主要用途：给盲操一个可随时瞄一眼的参考。
    add_guide_row(scr, 96, LV_SYMBOL_UP,
                  LV_SYMBOL_RIGHT " FF", LV_SYMBOL_NEXT " Next");
    add_guide_row(scr, 128, LV_SYMBOL_DOWN,
                  LV_SYMBOL_LEFT " REW", LV_SYMBOL_PREV " Prev");
    add_guide_row(scr, 160, LV_SYMBOL_OK,
                  LV_SYMBOL_PLAY " Play", "Fullscr");
    // 第 4 行：组合键。左侧「⏎+」表示按住 ⏎，中间/右侧是再按 ↑/↓ 的音量加/减
    // (ASCII 连字符，Montserrat 没有特殊减号字形)。
    (void)add_guide_row(scr, 192, LV_SYMBOL_OK "+",
                        LV_SYMBOL_UP " Vol+", LV_SYMBOL_DOWN " Vol-");

    s_action_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_action_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_action_label, lv_color_hex(0xFFD928), 0);
    lv_obj_align(s_action_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_text(s_action_label, "");

    s_last_input_us = esp_timer_get_time();
    ui_tick(NULL);                       // 先立即刷新一次，不等首个定时周期
    lv_timer_create(ui_tick, 500, NULL);
    lv_screen_load(scr);
}
