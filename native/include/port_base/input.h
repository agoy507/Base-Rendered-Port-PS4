#ifndef PORT_BASE_INPUT_H
#define PORT_BASE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortBaseButton {
    PORT_BASE_BUTTON_UP = 1u << 0,
    PORT_BASE_BUTTON_DOWN = 1u << 1,
    PORT_BASE_BUTTON_LEFT = 1u << 2,
    PORT_BASE_BUTTON_RIGHT = 1u << 3,
    PORT_BASE_BUTTON_CROSS = 1u << 4,
    PORT_BASE_BUTTON_CIRCLE = 1u << 5,
    PORT_BASE_BUTTON_SQUARE = 1u << 6,
    PORT_BASE_BUTTON_TRIANGLE = 1u << 7,
    PORT_BASE_BUTTON_L1 = 1u << 8,
    PORT_BASE_BUTTON_R1 = 1u << 9,
    PORT_BASE_BUTTON_L2 = 1u << 10,
    PORT_BASE_BUTTON_R2 = 1u << 11,
    PORT_BASE_BUTTON_OPTIONS = 1u << 12,
    PORT_BASE_BUTTON_TOUCH_PAD = 1u << 13,
    PORT_BASE_BUTTON_L3 = 1u << 14,
    PORT_BASE_BUTTON_R3 = 1u << 15
} PortBaseButton;

typedef struct PortBaseInputState {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
} PortBaseInputState;

bool port_base_cheat_menu_requested(const PortBaseInputState *input);

#ifdef __cplusplus
}
#endif

#endif
