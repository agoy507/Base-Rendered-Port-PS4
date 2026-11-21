#include <stdint.h>

#include "hmbtn/core.h"
#include "hmbtn/game.h"
#include "hmbtn/input.h"
#include "hmbtn/pixel.h"

#define HMBTN_EXPORT __declspec(dllexport)

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

HMBTN_EXPORT int hmbtn_test_performance_contract(void) {
    return HMBTN_OUTPUT_WIDTH == 1920u &&
           HMBTN_OUTPUT_HEIGHT == 1080u &&
           HMBTN_TARGET_FPS == 60u &&
           HMBTN_MINIMUM_FPS == 50u &&
           HMBTN_SIMULATION_STEP_US == 16667u &&
           HMBTN_MAX_FRAME_TIME_US == 20000u;
}

HMBTN_EXPORT int hmbtn_test_widescreen_viewport(void) {
    const HmbtnViewport viewport = hmbtn_viewport_fit(1280u, 720u, 1920u, 1080u);
    return viewport.x == 0u && viewport.y == 0u &&
           viewport.width == 1920u && viewport.height == 1080u;
}

HMBTN_EXPORT int hmbtn_test_fixed_step(void) {
    HmbtnFrameClock clock = hmbtn_frame_clock_create();
    if (hmbtn_frame_clock_advance(&clock, 16667u) != 1u) {
        return 0;
    }
    if (hmbtn_frame_clock_advance(&clock, 33334u) != 2u) {
        return 0;
    }
    if (hmbtn_frame_clock_advance(&clock, 1000000u) != HMBTN_MAX_CATCH_UP_STEPS) {
        return 0;
    }
    return clock.accumulator_us < HMBTN_SIMULATION_STEP_US;
}

HMBTN_EXPORT int hmbtn_test_gameplay_core(void) {
    HmbtnFixedClock clock = hmbtn_fixed_clock_create();
    uint32_t total_steps = 0u;
    for (uint32_t index = 0u; index < 100u; ++index) {
        const HmbtnFixedClockAdvance advance =
            hmbtn_fixed_clock_advance(clock, 10000u);
        clock = advance.clock;
        total_steps += advance.steps;
    }
    if (total_steps != 60u) {
        return 0;
    }

    const HmbtnGameState initial = hmbtn_game_create();
    const HmbtnInputState right = {HMBTN_BUTTON_RIGHT, 0u, 0u};
    HmbtnGameState moved = initial;
    for (uint32_t tick = 0u; tick < 60u; ++tick) {
        moved = hmbtn_game_step(&moved, &right);
    }
    if (hmbtn_game_player_x(&moved) != hmbtn_game_player_x(&initial) + 240 ||
        hmbtn_game_player_y(&moved) != hmbtn_game_player_y(&initial) ||
        moved.tick != 60u) {
        return 0;
    }

    const HmbtnInputState circle = {
        HMBTN_BUTTON_CIRCLE,
        HMBTN_BUTTON_CIRCLE,
        0u,
    };
    const HmbtnGameState after_circle = hmbtn_game_step(&initial, &circle);
    if (hmbtn_game_player_x(&after_circle) != hmbtn_game_player_x(&initial) ||
        hmbtn_game_player_y(&after_circle) != hmbtn_game_player_y(&initial)) {
        return 0;
    }

    HmbtnGameState edge = initial;
    edge.player_x_q16 = -100 * HMBTN_Q16_ONE;
    edge.player_y_q16 = -100 * HMBTN_Q16_ONE;
    edge = hmbtn_game_step(&edge, 0);
    const HmbtnCamera camera = hmbtn_camera_follow(&initial);
    HmbtnGameState farmhouse = initial;
    farmhouse.player_x_q16 = 666 * HMBTN_Q16_ONE;
    farmhouse.player_y_q16 = 320 * HMBTN_Q16_ONE;
    farmhouse = hmbtn_game_step(&farmhouse, &right);
    return hmbtn_game_player_x(&edge) == 0 &&
           hmbtn_game_player_y(&edge) == 0 &&
           camera.width == HMBTN_LOGICAL_WIDTH &&
           camera.height == HMBTN_LOGICAL_HEIGHT &&
           hmbtn_game_player_x(&farmhouse) == 668;
}

HMBTN_EXPORT int hmbtn_test_cheat_chord(void) {
    const HmbtnInputState circle = {
        HMBTN_BUTTON_CIRCLE,
        HMBTN_BUTTON_CIRCLE,
        0u,
    };
    const HmbtnInputState l3_only = {
        HMBTN_BUTTON_L3,
        HMBTN_BUTTON_L3,
        0u,
    };
    const HmbtnInputState chord = {
        HMBTN_BUTTON_L3 | HMBTN_BUTTON_R3,
        HMBTN_BUTTON_R3,
        0u,
    };
    return !hmbtn_cheat_menu_requested(&circle) &&
           !hmbtn_cheat_menu_requested(&l3_only) &&
           hmbtn_cheat_menu_requested(&chord);
}

HMBTN_EXPORT int hmbtn_test_pixels(void) {
    const uint32_t background = 0xFF102030u;
    return hmbtn_argb_to_abgr(0xFF112233u) == 0xFF332211u &&
           hmbtn_argb_to_abgr(0x8040A0F0u) == 0x80F0A040u &&
           hmbtn_blend_argb_over_abgr(0xFFFF0000u, background) == 0xFF0000FFu &&
           hmbtn_blend_argb_over_abgr(0x00FFFFFFu, background) == background;
}
