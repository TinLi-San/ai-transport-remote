// main/power_sleep.c —— 空闲省电：DFS + PM 自动 light sleep。
//
// 这里不直接调 esp_light_sleep_start()。原因：本板在 BLE 连接下用显式 light sleep
// 实测会把设备挂死（串口无响应、按键不能唤醒，只能断电重启）；改用 ESP-IDF 的
// 自动 light sleep（PM + tickless + BT modem sleep）这条官方路径。
//
// 工作方式：
//   - 开机配置 DFS（80/40 MHz），并持有一把 ESP_PM_NO_LIGHT_SLEEP 锁 —— 活跃期间
//     不允许自动睡眠；
//   - 空闲达到 POWER_POLICY_SLEEP_IDLE_MS、屏幕已灭、BLE 已连接、USB 未插入、按键
//     松开时：停 LVGL、DISPOFF 面板、把按键脚交接成 GPIO0 低电平唤醒，然后释放
//     PM 锁并阻塞等待 GPIO 中断。空闲任务随后自动进入 light sleep；
//   - 按键把 GPIO0 拉低 → 芯片被 GPIO 唤醒 → GPIO 中断唤醒守护任务 → 重建按键、
//     恢复 LVGL、重新持有 PM 锁。
//   - USB 插着时永不进入该流程（串口调试/充电）；PM 层另有
//     CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION 兜底。
#include "power_sleep.h"

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "media_remote.h"
#include "power_policy.h"

static const char *TAG = "power";

#define POWER_SLEEP_TASK_STACK      4096
#define POWER_SLEEP_TASK_PRIORITY   2
#define POWER_SLEEP_RETRY_MS        1000
#define POWER_SLEEP_CPU_MAX_MHZ     80
#define POWER_SLEEP_CPU_MIN_MHZ     40

static TaskHandle_t s_task;
static bsp_btn_cb_t s_btn_cb;
static volatile uint32_t s_last_activity_ms;
static bool s_usb_was_connected;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool usb_connected(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_is_connected();
#else
    return false;
#endif
}

#if POWER_POLICY_LIGHT_SLEEP_ENABLED
static SemaphoreHandle_t s_wake_sem;          // GPIO ISR -> 守护任务
static esp_pm_lock_handle_t s_active_lock;    // 活跃期间禁止自动 light sleep
static volatile uint32_t s_sleep_entries;     // PM 实际进入 light sleep 的次数
static volatile uint32_t s_sleep_exits;

static void IRAM_ATTR wake_isr(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_wake_sem, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static esp_err_t pm_enter_cb(int64_t sleep_time_us, void *arg)
{
    (void)sleep_time_us; (void)arg;
    s_sleep_entries++;
    return ESP_OK;
}

static esp_err_t pm_exit_cb(int64_t sleep_time_us, void *arg)
{
    (void)sleep_time_us; (void)arg;
    s_sleep_exits++;
    return ESP_OK;
}

static void report_stats(void)
{
    ESP_LOGI(TAG, "sleep stats: entries=%u exits=%u",
             (unsigned)s_sleep_entries, (unsigned)s_sleep_exits);
}

static bool key_held(void)
{
    const int mv = bsp_button_read_mv();
    return mv < 0 || mv < BSP_BTN_MV_RELEASED_MIN;
}

static bool can_sleep_now(void)
{
    return power_policy_can_sleep(media_remote_screen_is_on(),
                                  media_remote_is_connected(),
                                  usb_connected(), key_held());
}

static void disarm_gpio_wakeup(void)
{
    gpio_intr_disable((gpio_num_t)BSP_BTN_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)gpio_wakeup_disable((gpio_num_t)BSP_BTN_GPIO);
}

static void restore_buttons(void)
{
    if (bsp_button_init(s_btn_cb, NULL) == ESP_OK) return;
    ESP_LOGW(TAG, "light sleep 醒来后按键重建失败，100 ms 后重试");
    vTaskDelay(pdMS_TO_TICKS(100));
    if (bsp_button_init(s_btn_cb, NULL) == ESP_OK) return;
    ESP_LOGE(TAG, "按键重建连续失败，重启恢复外设");
    esp_restart();
}

// 准备好后释放 PM 锁并等待 GPIO 中断；返回时已完成恢复。
static void sleep_until_key(void)
{
    int level = 0;
    if (!bsp_lvgl_suspend()) {
        return;
    }
    (void)bsp_display_blank(true);
    if (bsp_button_prepare_light_sleep(&level) != ESP_OK) {
        ESP_LOGW(TAG, "按键交接失败，取消本次 light sleep");
        bsp_lvgl_resume();
        return;
    }
    if (level != 1) {
        ESP_LOGW(TAG, "交接时按键仍被按住，取消本次 light sleep");
        restore_buttons();
        bsp_lvgl_resume();
        return;
    }
    if (!power_policy_sleep_due(now_ms(), s_last_activity_ms)) {
        restore_buttons();
        bsp_lvgl_resume();
        return;
    }

    esp_err_t err = gpio_wakeup_enable((gpio_num_t)BSP_BTN_GPIO,
                                       GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO 唤醒武装失败(%s)，取消本次 light sleep",
                 esp_err_to_name(err));
        restore_buttons();
        bsp_lvgl_resume();
        return;
    }
    // 边沿中断只负责醒来后叫醒守护任务；电平唤醒由 gpio_wakeup_enable 管。
    gpio_set_intr_type((gpio_num_t)BSP_BTN_GPIO, GPIO_INTR_NEGEDGE);
    gpio_intr_enable((gpio_num_t)BSP_BTN_GPIO);

    // 清掉可能残留的信号，再释放 PM 锁让空闲任务进入自动 light sleep。
    xSemaphoreTake(s_wake_sem, 0);
    esp_pm_lock_release(s_active_lock);
    ESP_LOGI(TAG, "light sleep armed (idle=%u ms)",
             (unsigned)power_policy_elapsed_ms(now_ms(), s_last_activity_ms));

    // 自动 light sleep 发生在 idle 任务里；GPIO 按键唤醒后 ISR 给出信号。
    (void)xSemaphoreTake(s_wake_sem, portMAX_DELAY);

    // 回到活跃：先禁止唤醒源，再恢复外设与 PM 锁。
    esp_pm_lock_acquire(s_active_lock);
    disarm_gpio_wakeup();
    restore_buttons();
    bsp_lvgl_resume();
    s_last_activity_ms = now_ms();
    media_remote_wake_screen();
    ESP_LOGI(TAG, "light sleep wake ok, entries=%u exits=%u",
             (unsigned)s_sleep_entries, (unsigned)s_sleep_exits);
}

static void sleep_task(void *arg)
{
    (void)arg;
    for (;;) {
        const bool usb = usb_connected();
        if (usb != s_usb_was_connected) {
            s_usb_was_connected = usb;
            if (usb) report_stats();
        }

        const uint32_t elapsed = power_policy_elapsed_ms(now_ms(), s_last_activity_ms);
        if (elapsed < POWER_POLICY_SLEEP_IDLE_MS) {
            const uint32_t wait_ms = POWER_POLICY_SLEEP_IDLE_MS - elapsed;
            (void)xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(wait_ms));
            continue;
        }
        if (!power_policy_sleep_due(now_ms(), s_last_activity_ms)) {
            continue;
        }
        if (!can_sleep_now()) {
            vTaskDelay(pdMS_TO_TICKS(POWER_SLEEP_RETRY_MS));
            continue;
        }
        sleep_until_key();
    }
}
#endif // POWER_POLICY_LIGHT_SLEEP_ENABLED

esp_err_t power_sleep_init(bsp_btn_cb_t btn_cb)
{
    s_btn_cb = btn_cb;
    s_last_activity_ms = now_ms();
    s_usb_was_connected = usb_connected();
    (void)s_usb_was_connected;

#if !CONFIG_PM_ENABLE
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE 未启用，跳过 DFS/light sleep 配置");
    return ESP_OK;
#endif

    esp_pm_config_t cfg = {
        .max_freq_mhz = POWER_SLEEP_CPU_MAX_MHZ,
        .min_freq_mhz = POWER_SLEEP_CPU_MIN_MHZ,
#if POWER_POLICY_LIGHT_SLEEP_ENABLED
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };
    const esp_err_t pm_err = esp_pm_configure(&cfg);
    if (pm_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure 失败: %s", esp_err_to_name(pm_err));
        return pm_err;
    }
    ESP_LOGI(TAG, "PM: CPU_MAX=%d APB_MIN=%d, auto light sleep=%s",
             POWER_SLEEP_CPU_MAX_MHZ, POWER_SLEEP_CPU_MIN_MHZ,
#if POWER_POLICY_LIGHT_SLEEP_ENABLED
             "enabled"
#else
             "disabled"
#endif
    );

#if !POWER_POLICY_LIGHT_SLEEP_ENABLED
    return ESP_OK;
#else
    s_wake_sem = xSemaphoreCreateBinary();
    if (!s_wake_sem) return ESP_ERR_NO_MEM;

    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "active", &s_active_lock) !=
            ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_pm_lock_acquire(s_active_lock);

    const esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "GPIO ISR service 安装失败: %s", esp_err_to_name(isr_err));
        return isr_err;
    }
    if (gpio_isr_handler_add((gpio_num_t)BSP_BTN_GPIO, wake_isr, NULL) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_pm_sleep_cbs_register_config_t cbs = {
        .enter_cb = pm_enter_cb,
        .exit_cb = pm_exit_cb,
        .enter_cb_user_arg = NULL,
        .exit_cb_user_arg = NULL,
        .enter_cb_prior = 1,
        .exit_cb_prior = 1,
    };
    const esp_err_t cb_err = esp_pm_light_sleep_register_cbs(&cbs);
    if (cb_err != ESP_OK) {
        ESP_LOGW(TAG, "PM light-sleep 回调注册失败: %s", esp_err_to_name(cb_err));
    }

    if (xTaskCreate(sleep_task, "power_sleep", POWER_SLEEP_TASK_STACK, NULL,
                    POWER_SLEEP_TASK_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}

void power_sleep_activity(void)
{
    s_last_activity_ms = now_ms();
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}
