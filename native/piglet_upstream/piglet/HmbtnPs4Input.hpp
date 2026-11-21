#ifndef HMBTN_PS4_INPUT_HPP
#define HMBTN_PS4_INPUT_HPP

#include <cstdint>

#include "hmbtn/input.h"

class HmbtnPs4Input {
public:
    bool Initialize();
    HmbtnInputState Poll();

private:
    int32_t handle_{-1};
    uint32_t previous_buttons_{0u};
    uint32_t retry_frames_{0u};
};

#endif
