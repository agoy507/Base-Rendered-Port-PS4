#ifndef HMBTN_INPUT_H
#define HMBTN_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HmbtnButton {
    HMBTN_BUTTON_UP = 1u << 0,
    HMBTN_BUTTON_DOWN = 1u << 1,
    HMBTN_BUTTON_LEFT = 1u << 2,
    HMBTN_BUTTON_RIGHT = 1u << 3,
    HMBTN_BUTTON_CROSS = 1u << 4,
    HMBTN_BUTTON_CIRCLE = 1u << 5,
    HMBTN_BUTTON_SQUARE = 1u << 6,
    HMBTN_BUTTON_TRIANGLE = 1u << 7,
    HMBTN_BUTTON_L1 = 1u << 8,
    HMBTN_BUTTON_R1 = 1u << 9,
    HMBTN_BUTTON_L2 = 1u << 10,
    HMBTN_BUTTON_R2 = 1u << 11,
    HMBTN_BUTTON_OPTIONS = 1u << 12,
    HMBTN_BUTTON_TOUCH_PAD = 1u << 13,
    HMBTN_BUTTON_L3 = 1u << 14,
    HMBTN_BUTTON_R3 = 1u << 15
} HmbtnButton;

typedef struct HmbtnInputState {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
} HmbtnInputState;

bool hmbtn_cheat_menu_requested(const HmbtnInputState *input);

#ifdef __cplusplus
}
#endif

#endif
