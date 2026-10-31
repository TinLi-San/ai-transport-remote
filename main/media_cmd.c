// main/media_cmd.c —— 纯函数，宿主测试见 tests/test_media_cmd.c。
#include "media_cmd.h"

media_cmd_t media_cmd_map(int btn, int ev)
{
    switch (ev) {
    case MEDIA_EV_CLICK:
        if (btn == MEDIA_BTN_UP) return MEDIA_CMD_FAST_FORWARD;
        if (btn == MEDIA_BTN_DOWN) return MEDIA_CMD_REWIND;
        if (btn == MEDIA_BTN_OK) return MEDIA_CMD_PLAY_PAUSE;
        return MEDIA_CMD_NONE;
    case MEDIA_EV_LONG:
        if (btn == MEDIA_BTN_UP) return MEDIA_CMD_NEXT;
        if (btn == MEDIA_BTN_DOWN) return MEDIA_CMD_PREV;
        // 原来的「长按停止」已移除：Windows 上长按停止既不退出播放，恢复还要再长按一次。
        if (btn == MEDIA_BTN_OK) return MEDIA_CMD_FULLSCREEN;
        return MEDIA_CMD_NONE;
    case MEDIA_EV_CHORD:
        // 组合键：⏎ 已按住时再按一下 ↑/↓ = 音量加/减。BSP 只在组合成立时发这个事件。
        if (btn == MEDIA_BTN_UP) return MEDIA_CMD_VOLUME_UP;
        if (btn == MEDIA_BTN_DOWN) return MEDIA_CMD_VOLUME_DOWN;
        return MEDIA_CMD_NONE;
    default:
        return MEDIA_CMD_NONE;
    }
}
