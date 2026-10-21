#include "media_cmd.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    // 用户确认的映射：短按=快进/快退，长按=跳集。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_CLICK) == MEDIA_CMD_FAST_FORWARD);
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_LONG) == MEDIA_CMD_NEXT);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_CLICK) == MEDIA_CMD_REWIND);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_LONG) == MEDIA_CMD_PREV);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_CLICK) == MEDIA_CMD_PLAY_PAUSE);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_LONG) == MEDIA_CMD_STOP);

    // 非映射事件(按下瞬间/双击/抬起)一律不产生命令。
    assert(media_cmd_map(MEDIA_BTN_UP, MEDIA_EV_PRESS) == MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_OK, MEDIA_EV_DOUBLE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(MEDIA_BTN_DOWN, MEDIA_EV_RELEASE) == MEDIA_CMD_NONE);
    assert(media_cmd_map(99, MEDIA_EV_CLICK) == MEDIA_CMD_NONE);

    puts("media key mapping tests: PASS");
    return 0;
}