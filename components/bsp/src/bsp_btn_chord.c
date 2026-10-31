// components/bsp/src/bsp_btn_chord.c —— 顺序组合键状态机，宿主测试见 tests/test_bsp_chord.c。
#include "bsp_btn_chord.h"

// 防止 uint8 计数溢出后把"按住"判反；超过饱和值后的行为与饱和值一致。
static uint8_t sat_inc(uint8_t value)
{
    return value < UINT8_MAX ? (uint8_t)(value + 1) : UINT8_MAX;
}

void bsp_chord_reset(bsp_chord_t *chord)
{
    if (!chord) {
        return;
    }
    chord->raw = BSP_CHORD_WIN_OPEN;
    chord->raw_run = 0;
    chord->ok_run = 0;
    chord->masked = false;
}

bsp_chord_out_t bsp_chord_step(bsp_chord_t *chord, bsp_chord_win_t raw)
{
    bsp_chord_out_t out = { 0 };
    if (!chord) {
        return out;
    }

    // ⏎ 在本拍之前是否已经稳定按住：本拍可能是方向键的落下沿，
    // 所以必须在更新 ok_run 之前取这个值。
    const bool ok_held = chord->ok_run >= BSP_CHORD_OK_RUN;

    if ((bsp_chord_win_t)chord->raw == raw) {
        chord->raw_run = sat_inc(chord->raw_run);
    } else {
        chord->raw = (uint8_t)raw;
        chord->raw_run = 1;
    }
    chord->ok_run = raw == BSP_CHORD_WIN_OK ? sat_inc(chord->ok_run) : 0;

    const bool direction = raw == BSP_CHORD_WIN_UP || raw == BSP_CHORD_WIN_DOWN;
    const bsp_chord_action_t pressed = raw == BSP_CHORD_WIN_UP ? BSP_CHORD_ACTION_UP
                                             : raw == BSP_CHORD_WIN_DOWN ? BSP_CHORD_ACTION_DOWN
                                                                        : BSP_CHORD_ACTION_NONE;

    if (!chord->masked) {
        // ⏎ 已稳定按住 → 节点突然落到 ↑/↓ 窗口，说明 ⏎ 还按着、又按下了方向键。
        if (ok_held && direction) {
            chord->masked = true;
            out.action = pressed;
        }
    } else if (raw == BSP_CHORD_WIN_OPEN) {
        // 全部松手才结束这一组组合键。不能只看"离开方向键窗口"就结束：
        // 那可能只是先松了方向键、⏎ 还按着（此时节点回到 ⏎ 窗口）。
        // 也不能在方向键窗口里结束：先松 ⏎、方向键还按着时电压完全一样。
        if (chord->raw_run >= BSP_CHORD_RELEASE_RUN) {
            chord->masked = false;
        }
    } else if (ok_held && direction) {
        // 同一组里再按一次方向键（例如按住 ⏎ 连点 ↑）：每次按下发一步，按住不重复。
        out.action = pressed;
    }

    out.masked = chord->masked;
    return out;
}
