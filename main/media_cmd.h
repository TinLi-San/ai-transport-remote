// main/media_cmd.h —— 按键 → 媒体命令的纯映射。不依赖 ESP-IDF/LVGL，便于宿主测试。
#pragma once

// 媒体命令。前五条与 VOLUME_* 最终对应 HID 里的一个按键；
// FULLSCREEN 是键盘快捷键(Windows 与浏览器没有对应的系统媒体键，
// 只能模拟播放器自己的快捷键)。
typedef enum {
    MEDIA_CMD_NONE = 0,
    MEDIA_CMD_PLAY_PAUSE,   // 播放/暂停：Consumer Control 0xCD
    MEDIA_CMD_NEXT,         // 下一集：Consumer Control 0xB5 (Scan Next Track)
    MEDIA_CMD_PREV,         // 上一集：Consumer Control 0xB6 (Scan Previous Track)
    MEDIA_CMD_FAST_FORWARD, // 快进：键盘 →(见 hid_consumer.c，Windows 无对应媒体键)
    MEDIA_CMD_REWIND,       // 快退：键盘 ←
    MEDIA_CMD_FULLSCREEN,   // 全屏切换：键盘 f(B 站/YouTube 等网页播放器的全屏快捷键)
    MEDIA_CMD_VOLUME_UP,    // 音量加：Consumer Control 0xE9 (Volume Increment)
    MEDIA_CMD_VOLUME_DOWN,  // 音量减：Consumer Control 0xEA (Volume Decrement)
    MEDIA_CMD_COUNT
} media_cmd_t;

// 按键编号与事件编号刻意与 bsp_button.h 的 bsp_btn_t / bsp_btn_ev_t 取值一致
// (UP/DOWN/OK = 0/1/2；PRESS/CLICK/DOUBLE/LONG/RELEASE/CHORD = 0/1/2/3/4/5)，
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
    MEDIA_EV_CHORD,     // 组合键：⏎ 已按住时再按下 ↑/↓ 的瞬间(见 media_cmd_map)
} media_ev_t;

// 由「按键 + 事件」得到应发送的命令，无对应关系返回 MEDIA_CMD_NONE。
// 映射(用户已确认)：
//   ↑ 短按=快进,       ↑ 长按=下一集
//   ↓ 短按=快退,       ↓ 长按=上一集
//   ⏎ 短按=播放/暂停,  ⏎ 长按=全屏切换
//   ⏎ 按住 + ↑/↓ 按一下=音量加/减(组合键)
// 双击(DOUBLE)不映射任何动作，落到 default 返回 NONE(按键组件仍会发该事件，应用层忽略它)。
// 设备不需要知道电脑在播什么：所有键位都只依赖按键本身，不依赖播放状态。
media_cmd_t media_cmd_map(int btn, int ev);
