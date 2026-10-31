#include "media_cmd.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    // 短按=快进/快退，长按=跳集；⏎ 短按=播放/暂停，长按=全屏。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_CLICK) == MEDIA_CMD_FAST_FORWARD);
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_LONG) == MEDIA_CMD_NEXT);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_CLICK) == MEDIA_CMD_REWIND);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_LONG) == MEDIA_CMD_PREV);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_CLICK) == MEDIA_CMD_PLAY_PAUSE);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_LONG) == MEDIA_CMD_FULLSCREEN);

    // 双击：倍速与直播刷新方案都已放弃，DOUBLE 不映射任何动作。
    // 按键组件仍会发 DOUBLE 事件，由 media_remote.c 的事件白名单直接丢弃。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_DOUBLE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_DOUBLE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_DOUBLE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(99, MEDIA_EV_DOUBLE) == MEDIA_CMD_NONE);

    // 组合键(⏎ 按住 + ↑/↓)：音量加/减。
    // MEDIA_EV_CHORD 必须与 bsp_button.h 的 BSP_BTN_CHORD 取值一致(5)。
    assert(MEDIA_EV_CHORD == 5);
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_CHORD) == MEDIA_CMD_VOLUME_UP);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_CHORD) == MEDIA_CMD_VOLUME_DOWN);
    // ⏎ 自身与未知按键不参与组合键，不能产生音量命令。
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_CHORD) == MEDIA_CMD_NONE);
    assert(media_cmd_map(99, MEDIA_EV_CHORD) == MEDIA_CMD_NONE);
    // 组合键必须真的映射出命令(避免误写成 NONE 时测试仍然「通过」)。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_CHORD) != MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_CHORD) != MEDIA_CMD_NONE);
    // 落单的 ↑/↓ 事件语义不变：短按仍是快进/快退，长按仍是跳集。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_CLICK) != MEDIA_CMD_VOLUME_UP);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_LONG) != MEDIA_CMD_VOLUME_DOWN);

    // 原有的「长按 ⏎ = 停止」已删除，任何组合都不应再产生停止语义。
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_LONG) != MEDIA_CMD_PLAY_PAUSE);

    // 非映射事件(按下瞬间/抬起)一律不产生命令。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_PRESS) == MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_RELEASE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(99, MEDIA_EV_CLICK) == MEDIA_CMD_NONE);

    puts("media key mapping tests: PASS");
    return 0;
}
