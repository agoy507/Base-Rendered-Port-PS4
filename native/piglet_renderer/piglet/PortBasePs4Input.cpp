#include "PortBasePs4Input.hpp"
#include "PortBaseTelemetry.hpp"

#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

namespace {
constexpr uint8_t PORT_BASE_STICK_LOW = 80u;
constexpr uint8_t PORT_BASE_STICK_HIGH = 176u;

uint32_t mapButtons(const OrbisPadData& pad) {
    uint32_t buttons = 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_UP) != 0u ? PORT_BASE_BUTTON_UP : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_DOWN) != 0u ? PORT_BASE_BUTTON_DOWN : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_LEFT) != 0u ? PORT_BASE_BUTTON_LEFT : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_RIGHT) != 0u ? PORT_BASE_BUTTON_RIGHT : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_CROSS) != 0u ? PORT_BASE_BUTTON_CROSS : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_CIRCLE) != 0u ? PORT_BASE_BUTTON_CIRCLE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_SQUARE) != 0u ? PORT_BASE_BUTTON_SQUARE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_TRIANGLE) != 0u ? PORT_BASE_BUTTON_TRIANGLE : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L1) != 0u ? PORT_BASE_BUTTON_L1 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R1) != 0u ? PORT_BASE_BUTTON_R1 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L2) != 0u ? PORT_BASE_BUTTON_L2 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R2) != 0u ? PORT_BASE_BUTTON_R2 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_OPTIONS) != 0u ? PORT_BASE_BUTTON_OPTIONS : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) != 0u ? PORT_BASE_BUTTON_TOUCH_PAD : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_L3) != 0u ? PORT_BASE_BUTTON_L3 : 0u;
    buttons |= (pad.buttons & ORBIS_PAD_BUTTON_R3) != 0u ? PORT_BASE_BUTTON_R3 : 0u;

    buttons |= pad.leftStick.x < PORT_BASE_STICK_LOW ? PORT_BASE_BUTTON_LEFT : 0u;
    buttons |= pad.leftStick.x > PORT_BASE_STICK_HIGH ? PORT_BASE_BUTTON_RIGHT : 0u;
    buttons |= pad.leftStick.y < PORT_BASE_STICK_LOW ? PORT_BASE_BUTTON_UP : 0u;
    buttons |= pad.leftStick.y > PORT_BASE_STICK_HIGH ? PORT_BASE_BUTTON_DOWN : 0u;
    return buttons;
}
}

bool PortBasePs4Input::Initialize() {
    if (handle_ >= 0) {
        return true;
    }

    port_baseTelemetryCheckpoint("user-service-module-load-start");
    const int32_t user_module_result = static_cast<int32_t>(
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE));
    port_baseTelemetryCheckpoint("user-service-module-load-return");
    if (user_module_result < 0) {
        port_baseTelemetryCheckpoint("module-load-failed");
        return false;
    }

    port_baseTelemetryCheckpoint("pad-module-load-start");
    const int32_t pad_module_result = static_cast<int32_t>(
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD));
    port_baseTelemetryCheckpoint("pad-module-load-return");
    if (pad_module_result < 0) {
        port_baseTelemetryCheckpoint("module-load-failed");
        return false;
    }

    port_baseTelemetryCheckpoint("pad-init-start");
    const int32_t pad_result = scePadInit();
    if (pad_result < 0) {
        port_baseTelemetryCheckpoint("pad-init-failed");
        return false;
    }
    port_baseTelemetryCheckpoint("pad-init-ok");

    OrbisUserServiceInitializeParams params{};
    params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    port_baseTelemetryCheckpoint("user-service-init-start");
    sceUserServiceInitialize(&params);
    port_baseTelemetryCheckpoint("user-service-init-return");

    int32_t user_id = -1;
    const int32_t user_result = sceUserServiceGetInitialUser(&user_id);
    port_baseTelemetryCheckpoint("initial-user-return");
    if (user_result < 0) {
        return false;
    }
    handle_ = scePadOpen(user_id, 0, 0, nullptr);
    port_baseTelemetryCheckpoint("pad-open-return");
    return handle_ >= 0;
}

PortBaseInputState PortBasePs4Input::Poll() {
    if (handle_ < 0) {
        if (retry_frames_ > 0u) {
            retry_frames_ -= 1u;
            return PortBaseInputState{};
        }
        if (!Initialize()) {
            retry_frames_ = 120u;
            return PortBaseInputState{};
        }
    }
    OrbisPadData pad{};
    if (handle_ < 0 || scePadReadState(handle_, &pad) < 0) {
        return PortBaseInputState{};
    }
    const uint32_t buttons = mapButtons(pad);
    const PortBaseInputState input{
        buttons,
        buttons & ~previous_buttons_,
        previous_buttons_ & ~buttons,
    };
    previous_buttons_ = buttons;
    return input;
}
