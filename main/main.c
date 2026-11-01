// main/main.c —— 蓝牙播控器应用入口：BSP 初始化 + 按键分发。UI 与 BLE HID 见 media_remote.c。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "media_remote.h"
#include "power_policy.h"
#include "power_sleep.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

#define INPUT_QUEUE_DEPTH 8

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 深睡按键唤醒守卫：本应用只做 light sleep（不重启、按键组件由本应用重建），
// 守卫保持不武装，但必须按约定先于就绪检查调用，保证 release 事件也能先行撤防。
static key_guard_t s_key_guard;

// 按键回调跑在共享 esp_timer 任务里：只入队，立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        return;
    }
    // 任意按键都重置 light sleep 的空闲计时（任务里只做定时器读取与通知）。
    power_sleep_activity();
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            media_remote_key(input.btn, input.event);
        }
    }
}

static esp_err_t input_init(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "media_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// 本应用不播放/录音，但 ES8311 上电默认态仍可能耗电（数据手册正常 8 mA / suspend≈0）。
// 开机初始化后立即挂起；失败只告警，不影响遥控功能。
static void audio_suspend_unused(void)
{
    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) {
        err = bsp_audio_sleep();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 suspend 失败: %s（codec 可能停在默认态）",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "ES8311 已挂起（本应用不使用音频）");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "AI Transport media remote");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed");
        return;
    }
    bsp_display_backlight(POWER_POLICY_BACKLIGHT_ON_PERCENT);

    if (input_init() != ESP_OK) {
        ESP_LOGE(TAG, "input init failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;
    }
    bsp_battery_init();  // 失败不阻塞：UI 上显示 "--"
    audio_suspend_unused();

    if (bsp_lvgl_lock(1000)) {
        media_remote_ui_build();
        bsp_lvgl_unlock();
        s_input_ready = true;
    } else {
        ESP_LOGE(TAG, "LVGL lock failed");
        return;
    }

    // 先把 DFS 配好再启动 BLE（IDF NimBLE power_save 示例也是这个顺序）；睡眠任务
    // 在屏幕熄灭 + BLE 连接 + 空闲 60 s 之前不会真正入睡。
    const esp_err_t ps = power_sleep_init(on_key);
    if (ps != ESP_OK) {
        ESP_LOGW(TAG, "power_sleep init failed: %s", esp_err_to_name(ps));
    }

    const esp_err_t err = media_remote_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE HID init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "ready");
    }
}