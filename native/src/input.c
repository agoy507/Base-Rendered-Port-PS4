#include "hmbtn/input.h"

bool hmbtn_cheat_menu_requested(const HmbtnInputState *input) {
    const uint32_t chord = HMBTN_BUTTON_L3 | HMBTN_BUTTON_R3;
    if (input == 0 || (input->held & chord) != chord) {
        return false;
    }

    return (input->pressed & chord) != 0u;
}
