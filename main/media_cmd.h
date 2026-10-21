// main/media_cmd.h —— 按键 → 媒体命令的纯映射。不依赖 ESP-IDF/LVGL，便于宿主测试。
#pragma once

// 媒体命令。每一条最终对应 HID Consumer Control 页里的一个由 report map 声明的 usage。
typedef enum {
    MEDIA_CMD_NONE = 0,
    MEDIA_CMD_PLAY_PAUSE,   // 播放/暂停 0xCD
    MEDIA_CMD_NEXT,         // 下一集 0xB5 (Scan Next Track)
    MEDIA_CMD_PREV,         // 上一集 0xB6 (Scan Previous Track)
    MEDIA_CMD_FAST_FORWARD, // 快进 0xB3
    MEDIA_CMD_REWIND,       // 快退 0xB4
    MEDIA_CMD_STOP,         // 停止 0xB7
    MEDIA_CMD_COUNT
} media_cmd_t;

// 按键编号与事件编号刻意与 bsp_button.h 的 bsp_btn_t / bsp_btn_ev_t 取值一致
// (UP/DOWN/OK = 0/1/2；PRESS/CLICK/DOUBLE/LONG/RELEASE = 0/1/2/3/4)，
// 使上层能直接强转，而这里保持零依赖、可被 -std=c11 宿主测试覆盖。
typedef enum {
    MEDIA_BTN_UP = 0,
    MEDIA_BTN_DOWN,
    MEDIA_BTN_OK,
} media_btn_t;

typedef enum {
    MEDIA_EV_PRESS = 0,
    MEDIA_EV_CLICK,
    MEDIA_EV_DOUBLE,
    MEDIA_EV_LONG,
    MEDIA_EV_RELEASE,
} media_ev_t;

// 由「按键 + 事件」得到应发送的媒体命令，无对应关系返回 MEDIA_CMD_NONE。
// 映射(用户已确认)：
//   ↑ 短按=快进, ↑ 长按=下一集
//   ↓ 短按=快退, ↓ 长按=上一集
//   ⏎ 短按=播放/暂停, ⏎ 长按=停止
media_cmd_t media_cmd_map(int btn, int ev);