// components/bsp/src/bsp_btn_chord.h
// 顺序组合键（先按住 ⏎，再按 ↑/↓）的纯状态机。不依赖 ESP-IDF，宿主测试见
// tests/test_bsp_chord.c。
//
// 为什么只能做"顺序"而不是"同时"：三键共用一个 ADC 节点，↑ 是 0Ω 直通地，
// 任何含 ↑ 的组合与单按 ↑ 的电压完全相同；↓+⏎ = 212mV 又落在 ↓ 自己的窗口里。
// 电气上永远看不到"两个键同时按着"。但人手的先后顺序是看得见的：先看到 ⏎ 窗口
// （≈595mV）连续两拍，紧接着节点变成 ↑ 窗口（0mV）或 ↓ 窗口（≈212~300mV），
// 就说明 ⏎ 还按着、又按下了方向键 —— 这正是本状态机要识别的组合键。
// 电路事实与实测数据见 notes/key-chord-limit.md。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ADC 采样值落入哪个电压窗口。取值必须与 bsp_btn_t 的按键顺序一致
// (UP/DOWN/OK = 1/2/3)，bsp_button.c 用 BSP_CHORD_WIN_UP + 按键索引换算。
typedef enum {
    BSP_CHORD_WIN_OPEN = 0,  // 松开：高于最高按键窗口(约 3300mV)
    BSP_CHORD_WIN_UP,        // ↑ 窗口：0~150mV
    BSP_CHORD_WIN_DOWN,      // ↓ 窗口：150~447mV(同时按 ↓+⏎ 是 212mV，也在这里)
    BSP_CHORD_WIN_OK,        // ⏎ 窗口：447~1900mV
} bsp_chord_win_t;

// 本拍要发出的组合键动作。只有"方向键这一次按下的边沿"才会给出非 NONE，
// 按住不放不会重复发。
typedef enum {
    BSP_CHORD_ACTION_NONE = 0,
    BSP_CHORD_ACTION_UP,
    BSP_CHORD_ACTION_DOWN,
} bsp_chord_action_t;

// 判定参数，单位是"拍"（button 组件轮询周期，默认 10ms）。
// ⏎ 必须连续在窗口内两拍(约 20ms)才算按住：单拍多半是按键触点的抖动，
// 若拿它来当组合键的修饰键，按 ↑ 时的抖动可能被误判成"按过 ⏎"。
#define BSP_CHORD_OK_RUN      2
// 组合键组要连续两拍到松开态才结束：一拍的毛刺(线缆/ADC 噪声)不该把组打断，
// 否则同一组手势会被拆成两次，方向键的单击事件也可能漏出去。
#define BSP_CHORD_RELEASE_RUN 2

typedef struct {
    uint8_t raw;        // 上一拍的原始窗口
    uint8_t raw_run;    // 连续落在 raw 的拍数
    uint8_t ok_run;     // 连续落在 ⏎ 窗口的拍数
    bool    masked;     // 组合键组进行中：三个键都不得向按键组件报"按下"
} bsp_chord_t;

typedef struct {
    bool               masked;  // true: 本拍三键都报 INACTIVE
    bsp_chord_action_t action;  // 本拍新产生的组合键动作
} bsp_chord_out_t;

// 清零状态。按键初始化/清理时调用。
void bsp_chord_reset(bsp_chord_t *chord);

// 喂入一拍原始窗口，返回本拍应该怎么报给 iot_button。
bsp_chord_out_t bsp_chord_step(bsp_chord_t *chord, bsp_chord_win_t raw);
