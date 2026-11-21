#include <stdint.h>

#include "port_base/core.h"
#include "port_base/game.h"
#include "port_base/input.h"
#include "port_base/pixel.h"

#define PORT_BASE_EXPORT __declspec(dllexport)

void *memset(void *destination, int value, unsigned long long count) {
    unsigned char *bytes = (unsigned char *)destination;
    for (unsigned long long index = 0u; index < count; ++index) {
        bytes[index] = (unsigned char)value;
    }
    return destination;
}

int DllMain(void *instance, unsigned long reason, void *reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return 1;
}

PORT_BASE_EXPORT int port_base_test_performance_contract(void) {
    return PORT_BASE_OUTPUT_WIDTH == 1920u &&
           PORT_BASE_OUTPUT_HEIGHT == 1080u &&
           PORT_BASE_TARGET_FPS == 60u &&
           PORT_BASE_MINIMUM_FPS == 50u &&
           PORT_BASE_SIMULATION_STEP_US == 16667u &&
           PORT_BASE_MAX_FRAME_TIME_US == 20000u;
}

PORT_BASE_EXPORT int port_base_test_widescreen_viewport(void) {
    const PortBaseViewport viewport = port_base_viewport_fit(1280u, 720u, 1920u, 1080u);
    return viewport.x == 0u && viewport.y == 0u &&
           viewport.width == 1920u && viewport.height == 1080u;
}

PORT_BASE_EXPORT int port_base_test_fixed_step(void) {
    PortBaseFrameClock clock = port_base_frame_clock_create();
    if (port_base_frame_clock_advance(&clock, 16667u) != 1u) {
        return 0;
    }
    if (port_base_frame_clock_advance(&clock, 33334u) != 2u) {
        return 0;
    }
    if (port_base_frame_clock_advance(&clock, 1000000u) != PORT_BASE_MAX_CATCH_UP_STEPS) {
        return 0;
    }
    return clock.accumulator_us < PORT_BASE_SIMULATION_STEP_US;
}

PORT_BASE_EXPORT int port_base_test_gameplay_core(void) {
    PortBaseFixedClock clock = port_base_fixed_clock_create();
    uint32_t total_steps = 0u;
    for (uint32_t index = 0u; index < 100u; ++index) {
        const PortBaseFixedClockAdvance advance =
            port_base_fixed_clock_advance(clock, 10000u);
        clock = advance.clock;
        total_steps += advance.steps;
    }
    if (total_steps != 60u) {
        return 0;
    }

    const PortBaseGameState initial = port_base_game_create();
    const PortBaseInputState right = {PORT_BASE_BUTTON_RIGHT, 0u, 0u};
    PortBaseGameState moved = initial;
    for (uint32_t tick = 0u; tick < 60u; ++tick) {
        moved = port_base_game_step(&moved, &right);
    }
    if (port_base_game_player_x(&moved) != port_base_game_player_x(&initial) + 240 ||
        port_base_game_player_y(&moved) != port_base_game_player_y(&initial) ||
        moved.tick != 60u) {
        return 0;
    }

    const PortBaseInputState circle = {
        PORT_BASE_BUTTON_CIRCLE,
        PORT_BASE_BUTTON_CIRCLE,
        0u,
    };
    const PortBaseGameState after_circle = port_base_game_step(&initial, &circle);
    if (port_base_game_player_x(&after_circle) != port_base_game_player_x(&initial) ||
        port_base_game_player_y(&after_circle) != port_base_game_player_y(&initial)) {
        return 0;
    }

    PortBaseGameState edge = initial;
    edge.player_x_q16 = -100 * PORT_BASE_Q16_ONE;
    edge.player_y_q16 = -100 * PORT_BASE_Q16_ONE;
    edge = port_base_game_step(&edge, 0);
    const PortBaseCamera camera = port_base_camera_follow(&initial);
    PortBaseGameState farmhouse = initial;
    farmhouse.player_x_q16 = 666 * PORT_BASE_Q16_ONE;
    farmhouse.player_y_q16 = 320 * PORT_BASE_Q16_ONE;
    farmhouse = port_base_game_step(&farmhouse, &right);
    return port_base_game_player_x(&edge) == 0 &&
           port_base_game_player_y(&edge) == 0 &&
           camera.width == PORT_BASE_LOGICAL_WIDTH &&
           camera.height == PORT_BASE_LOGICAL_HEIGHT &&
           port_base_game_player_x(&farmhouse) == 668;
}

PORT_BASE_EXPORT int port_base_test_cheat_chord(void) {
    const PortBaseInputState circle = {
        PORT_BASE_BUTTON_CIRCLE,
        PORT_BASE_BUTTON_CIRCLE,
        0u,
    };
    const PortBaseInputState l3_only = {
        PORT_BASE_BUTTON_L3,
        PORT_BASE_BUTTON_L3,
        0u,
    };
    const PortBaseInputState chord = {
        PORT_BASE_BUTTON_L3 | PORT_BASE_BUTTON_R3,
        PORT_BASE_BUTTON_R3,
        0u,
    };
    return !port_base_cheat_menu_requested(&circle) &&
           !port_base_cheat_menu_requested(&l3_only) &&
           port_base_cheat_menu_requested(&chord);
}

PORT_BASE_EXPORT int port_base_test_pixels(void) {
    const uint32_t background = 0xFF102030u;
    return port_base_argb_to_abgr(0xFF112233u) == 0xFF332211u &&
           port_base_argb_to_abgr(0x8040A0F0u) == 0x80F0A040u &&
           port_base_blend_argb_over_abgr(0xFFFF0000u, background) == 0xFF0000FFu &&
           port_base_blend_argb_over_abgr(0x00FFFFFFu, background) == background;
}
