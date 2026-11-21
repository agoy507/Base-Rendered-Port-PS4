#include "port_base/input.h"

bool port_base_cheat_menu_requested(const PortBaseInputState *input) {
    const uint32_t chord = PORT_BASE_BUTTON_L3 | PORT_BASE_BUTTON_R3;
    if (input == 0 || (input->held & chord) != chord) {
        return false;
    }

    return (input->pressed & chord) != 0u;
}
