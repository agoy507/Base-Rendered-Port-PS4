#ifndef PORT_BASE_PS4_INPUT_HPP
#define PORT_BASE_PS4_INPUT_HPP

#include <cstdint>

#include "port_base/input.h"

class PortBasePs4Input {
public:
    bool Initialize();
    PortBaseInputState Poll();

private:
    int32_t handle_{-1};
    uint32_t previous_buttons_{0u};
    uint32_t retry_frames_{0u};
};

#endif
