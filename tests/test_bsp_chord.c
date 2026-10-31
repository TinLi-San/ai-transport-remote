// 顺序组合键状态机的宿主测试（纯逻辑，不依赖 ESP-IDF）。
// 运行方式见 tools/validate.sh：zig/gcc -Icomponents/bsp/src \
//     tests/test_bsp_chord.c components/bsp/src/bsp_btn_chord.c
#include "bsp_btn_chord.h"

#include <assert.h>
#include <stdio.h>

// 一拍 = button 组件的一次轮询（默认 10ms）。这里不关心时间，只关心窗口序列。
static bsp_chord_out_t tick(bsp_chord_t *c, bsp_chord_win_t win)
{
    return bsp_chord_step(c, win);
}

static void feed(bsp_chord_t *c, bsp_chord_win_t win, int times)
{
    for (int i = 0; i < times; ++i) {
        assert(tick(c, win).action == BSP_CHORD_ACTION_NONE);
    }
}

int main(void)
{
    bsp_chord_t c;
    bsp_chord_reset(&c);

    // 松开态：不屏蔽、不动作。
    bsp_chord_out_t out = tick(&c, BSP_CHORD_WIN_OPEN);
    assert(!out.masked && out.action == BSP_CHORD_ACTION_NONE);

    // 只按 ↑：永远不会产生组合键动作。
    feed(&c, BSP_CHORD_WIN_UP, 5);
    assert(!tick(&c, BSP_CHORD_WIN_UP).masked);

    // ⏎ 按住两拍后再按 ↑：识别为组合键，且从这一拍起三键全部屏蔽。
    bsp_chord_reset(&c);
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(out.masked && out.action == BSP_CHORD_ACTION_UP);
    // 按住方向键不放只发一次动作（音量按一次一步，不自动重复）。
    for (int i = 0; i < 10; ++i) {
        out = tick(&c, BSP_CHORD_WIN_UP);
        assert(out.masked && out.action == BSP_CHORD_ACTION_NONE);
    }

    // 松开方向键、⏎ 仍按住：仍然屏蔽（否则 ⏎ 会被组件重新当成一次新按下，
    // 松手后补出一次播放/暂停）。
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN);
    out = tick(&c, BSP_CHORD_WIN_OK);
    assert(out.masked && out.action == BSP_CHORD_ACTION_NONE);

    // 同一组里再点一次 ↑：再发一步。
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(out.masked && out.action == BSP_CHORD_ACTION_UP);
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN);
    out = tick(&c, BSP_CHORD_WIN_DOWN);
    assert(out.masked && out.action == BSP_CHORD_ACTION_DOWN);

    // 全部松手：连续 BSP_CHORD_RELEASE_RUN 拍后解除屏蔽。
    feed(&c, BSP_CHORD_WIN_OK, 1);
    out = tick(&c, BSP_CHORD_WIN_OPEN);
    assert(out.masked);                       // 一拍毛刺不算松手
    out = tick(&c, BSP_CHORD_WIN_OPEN);
    assert(!out.masked && out.action == BSP_CHORD_ACTION_NONE);

    // 解除屏蔽后普通按键（↑ 单击、⏎ 单击）不会被误判成组合键。
    feed(&c, BSP_CHORD_WIN_UP, 3);
    assert(!tick(&c, BSP_CHORD_WIN_UP).masked);
    feed(&c, BSP_CHORD_WIN_OPEN, BSP_CHORD_RELEASE_RUN);
    feed(&c, BSP_CHORD_WIN_OK, 3);
    assert(!tick(&c, BSP_CHORD_WIN_OK).masked);

    // ⏎ 只按了一拍就按 ↑：不算按住 ⏎（多半是按键抖动），按普通 ↑ 处理。
    bsp_chord_reset(&c);
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN - 1);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(!out.masked && out.action == BSP_CHORD_ACTION_NONE);

    // 先按 ↑ 再按 ⏎：电气上不可识别（节点始终 0mV），绝不能凭空造出组合键。
    bsp_chord_reset(&c);
    feed(&c, BSP_CHORD_WIN_UP, 5);
    feed(&c, BSP_CHORD_WIN_OK, 5);            // 现实中这里电压仍是 0mV
    assert(!tick(&c, BSP_CHORD_WIN_OK).masked);

    // ⏎ 按住超过 uint8 饱和值也不会把"按住"判反。
    bsp_chord_reset(&c);
    feed(&c, BSP_CHORD_WIN_OK, 300);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(out.masked && out.action == BSP_CHORD_ACTION_UP);

    // 组合期间方向键窗口被噪声短暂打断（回到 ⏎ 窗口一拍后又是方向键）：
    // 这是"松开再按"，应当再发一步。
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(out.action == BSP_CHORD_ACTION_UP);
    out = tick(&c, BSP_CHORD_WIN_OK);
    assert(out.masked && out.action == BSP_CHORD_ACTION_NONE);
    feed(&c, BSP_CHORD_WIN_OK, BSP_CHORD_OK_RUN - 1);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(out.masked && out.action == BSP_CHORD_ACTION_UP);

    // reset 必须彻底回到初始状态（按键初始化/清理路径依赖这一点）。
    bsp_chord_reset(&c);
    assert(c.raw == BSP_CHORD_WIN_OPEN && c.raw_run == 0 && c.ok_run == 0 && !c.masked);
    out = tick(&c, BSP_CHORD_WIN_UP);
    assert(!out.masked && out.action == BSP_CHORD_ACTION_NONE);

    puts("BSP chord (sequential OK + UP/DOWN) tests: PASS");
    return 0;
}
