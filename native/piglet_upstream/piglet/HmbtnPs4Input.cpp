#include "HmbtnPs4Input.hpp"
#include "HmbtnTelemetry.hpp"

#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

namespace {
constexpr uint8_t HMBTN_STICK_LOW = 80u;
constexpr uint8_t HMBTN_STICK_HIGH = 176u;

uint32_t mapButtons(const OrbisPadData& pad) {
    uint32_t buttons = 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_UP) != 0u ? HMBTN_BUTTON_UP : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_DOWN) != 0u ? HMBTN_BUTTON_DOWN : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_LEFT) != 0u ? HMBTN_BUTTON_LEFT : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_RIGHT) != 0u ? HMBTN_BUTTON_RIGHT : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_CROSS) != 0u ? HMBTN_BUTTON_CROSS : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_CIRCLE) != 0u ? HMBTN_BUTTON_CIRCLE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_SQUARE) != 0u ? HMBTN_BUTTON_SQUARE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_TRIANGLE) != 0u ? HMBTN_BUTTON_TRIANGLE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L1) != 0u ? HMBTN_BUTTON_L1 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R1) != 0u ? HMBTN_BUTTON_R1 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L2) != 0u ? HMBTN_BUTTON_L2 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R2) != 0u ? HMBTN_BUTTON_R2 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_OPTIONS) != 0u ? HMBTN_BUTTON_OPTIONS : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) != 0u ? HMBTN_BUTTON_TOUCH_PAD : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L3) != 0u ? HMBTN_BUTTON_L3 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R3) != 0u ? HMBTN_BUTTON_R3 : 0u;

    buttons |= pad.leftStick.x < HMBTN_STICK_LOW ? HMBTN_BUTTON_LEFT : 0u;
    buttons |= pad.leftStick.x > HMBTN_STICK_HIGH ? HMBTN_BUTTON_RIGHT : 0u;
    buttons |= pad.leftStick.y < HMBTN_STICK_LOW ? HMBTN_BUTTON_UP : 0u;
    buttons |= pad.leftStick.y > HMBTN_STICK_HIGH ? HMBTN_BUTTON_DOWN : 0u;
    return buttons;
}
}

bool HmbtnPs4Input::Initialize() {
    if (handle_ >= 0) {
        return true;
    }

    hmbtnTelemetryCheckpoint("user-service-module-load-start");
    const int32_t user_module_result = static_cast<int32_t>(
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE));
    hmbtnTelemetryCheckpoint("user-service-module-load-return");
    if (user_module_result < 0) {
        hmbtnTelemetryCheckpoint("module-load-failed");
        return false;
    }

    hmbtnTelemetryCheckpoint("pad-module-load-start");
    const int32_t pad_module_result = static_cast<int32_t>(
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD));
    hmbtnTelemetryCheckpoint("pad-module-load-return");
    if (pad_module_result < 0) {
        hmbtnTelemetryCheckpoint("module-load-failed");
        return false;
    }

    hmbtnTelemetryCheckpoint("pad-init-start");
    const int32_t pad_result = scePadInit();
    if (pad_result < 0) {
        hmbtnTelemetryCheckpoint("pad-init-failed");
        return false;
    }
    hmbtnTelemetryCheckpoint("pad-init-ok");

    OrbisUserServiceInitializeParams params{};
    params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    hmbtnTelemetryCheckpoint("user-service-init-start");
    sceUserServiceInitialize(&params);
    hmbtnTelemetryCheckpoint("user-service-init-return");

    int32_t user_id = -1;
    const int32_t user_result = sceUserServiceGetInitialUser(&user_id);
    hmbtnTelemetryCheckpoint("initial-user-return");
    if (user_result < 0) {
        return false;
    }
    handle_ = scePadOpen(user_id, 0, 0, nullptr);
    hmbtnTelemetryCheckpoint("pad-open-return");
    return handle_ >= 0;
}

HmbtnInputState HmbtnPs4Input::Poll() {
    if (handle_ < 0) {
        if (retry_frames_ > 0u) {
            retry_frames_ -= 1u;
            return HmbtnInputState{};
        }
        if (!Initialize()) {
            retry_frames_ = 120u;
            return HmbtnInputState{};
        }
    }
    OrbisPadData pad{};
    if (handle_ < 0 || scePadReadState(handle_, &pad) < 0) {
        return HmbtnInputState{};
    }
    const uint32_t buttons = mapButtons(pad);
    const HmbtnInputState input{
        buttons,
        buttons & ~previous_buttons_,
        previous_buttons_ & ~buttons,
    };
    previous_buttons_ = buttons;
    return input;
}
