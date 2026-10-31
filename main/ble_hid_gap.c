// main/ble_hid_gap.c —— 移植自 esp-idf 示例
// examples/bluetooth/esp_hid_device/main/esp_hid_gap.c 的 NimBLE 分支
// (Apache-2.0 / CC0-1.0, Espressif Systems)，只保留媒体遥控需要的部分。
// 配对策略用 Just Works(NO_IO)：Windows 无需输入配对码即可绑定 HID 设备。
#include "ble_hid_gap.h"

#include <inttypes.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_log.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble_hid_gap";

#define GATT_SVR_SVC_HID_UUID 0x1812
#define HID_APPEARANCE_GENERIC 0x03C0

static struct ble_hs_adv_fields s_adv_fields;

static int gap_event_cb(struct ble_gap_event *event, void *arg);

esp_err_t ble_hid_gap_init(void)
{
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller mem_release failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t ble_hid_gap_adv_init(const char *device_name)
{
    memset(&s_adv_fields, 0, sizeof(s_adv_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = HID_APPEARANCE_GENERIC;
    s_adv_fields.appearance_is_present = 1;
    // 广播包上限 31 字节：flags(3)+appearance(4)+name(2+L)+UUID16(4)。这里不放
    // tx_pwr(省 3 字节)以保证足够余量，避免 ble_gap_adv_set_fields 返回 EMSGSIZE。
    s_adv_fields.name = (const uint8_t *)device_name;
    s_adv_fields.name_len = strlen(device_name);
    s_adv_fields.name_is_complete = 1;

    static ble_uuid16_t hid_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID);
    s_adv_fields.uuids16 = &hid_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    // Just Works：无配对码，绑定 + 加密链路，HID 报告被 Windows 正常接收。
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    return ESP_OK;
}

esp_err_t ble_hid_gap_adv_start(void)
{
    int rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: rc=%d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    // duration_ms = BLE_HS_FOREVER → 永久广播(断开后在 ESP_HIDD_DISCONNECT 回调里重开)。
    // 注意：传 0 表示“0 毫秒”会立即停止广播，必须用 BLE_HS_FOREVER(INT32_MAX)。
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection established, status=%d", event->connect.status);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect, reason=%d", event->disconnect.reason);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption change, status=%d", event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // 设备重启后 Windows 携带旧 bond 重连：丢弃旧 bond 接受新链路(Just Works 静默完成)。
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "advertising stopped, reason=%d", event->adv_complete.reason);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
    case BLE_GAP_EVENT_CONN_UPDATE:
        return 0;
    case BLE_GAP_EVENT_MTU:
        // 诊断用：确认链路上能承载 160 字节的厂商 HID 输出报告。
        ESP_LOGI(TAG, "mtu update: conn=%u mtu=%u", event->mtu.conn_handle,
                 event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_TX:
        return 0;
    default:
        return 0;
    }
}

void ble_hid_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "nimble host task running");
    nimble_port_run();
    nimble_port_freertos_deinit();
}