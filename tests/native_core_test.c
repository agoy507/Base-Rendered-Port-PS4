#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "hmbtn/core.h"
#include "hmbtn/game.h"
#include "hmbtn/input.h"
#include "hmbtn/pixel.h"

static void test_performance_contract(void) {
    assert(HMBTN_OUTPUT_WIDTH == 1920u);
    assert(HMBTN_OUTPUT_HEIGHT == 1080u);
    assert(HMBTN_TARGET_FPS == 60u);
    assert(HMBTN_MINIMUM_FPS == 50u);
    assert(HMBTN_SIMULATION_STEP_US == 16667u);
    assert(HMBTN_MAX_FRAME_TIME_US == 20000u);
}

static void test_sixteen_by_nine_viewport_fills_1080p(void) {
    const HmbtnViewport viewport = hmbtn_viewport_fit(
        1280u, 720u, HMBTN_OUTPUT_WIDTH, HMBTN_OUTPUT_HEIGHT);

    assert(viewport.x == 0u);
    assert(viewport.y == 0u);
    assert(viewport.width == 1920u);
    assert(viewport.height == 1080u);
}

static void test_fixed_step_is_bounded(void) {
    HmbtnFrameClock clock = hmbtn_frame_clock_create();

    assert(hmbtn_frame_clock_advance(&clock, 16667u) == 1u);
    assert(hmbtn_frame_clock_advance(&clock, 33334u) == 2u);
    assert(hmbtn_frame_clock_advance(&clock, 1000000u) == HMBTN_MAX_CATCH_UP_STEPS);
    assert(clock.accumulator_us < HMBTN_SIMULATION_STEP_US);
}

static void test_exact_sixty_hz_clock(void) {
    HmbtnFixedClock clock = hmbtn_fixed_clock_create();
    uint32_t total_steps = 0u;
    for (uint32_t index = 0u; index < 100u; ++index) {
        const HmbtnFixedClockAdvance advance =
            hmbtn_fixed_clock_advance(clock, 10000u);
        clock = advance.clock;
        total_steps += advance.steps;
    }
    assert(total_steps == 60u);
}

static void test_gameplay_movement_collision_and_camera(void) {
    const HmbtnGameState initial = hmbtn_game_create();
    const HmbtnInputState right = {.held = HMBTN_BUTTON_RIGHT};
    HmbtnGameState moved = initial;
    for (uint32_t tick = 0u; tick < 60u; ++tick) {
        moved = hmbtn_game_step(&moved, &right);
    }
    assert(hmbtn_game_player_x(&moved) == hmbtn_game_player_x(&initial) + 240);
    assert(hmbtn_game_player_y(&moved) == hmbtn_game_player_y(&initial));
    assert(moved.tick == 60u);

    const HmbtnInputState circle = {
        .held = HMBTN_BUTTON_CIRCLE,
        .pressed = HMBTN_BUTTON_CIRCLE,
    };
    const HmbtnGameState after_circle = hmbtn_game_step(&initial, &circle);
    assert(hmbtn_game_player_x(&after_circle) == hmbtn_game_player_x(&initial));
    assert(hmbtn_game_player_y(&after_circle) == hmbtn_game_player_y(&initial));

    HmbtnGameState edge = initial;
    edge.player_x_q16 = -100 * HMBTN_Q16_ONE;
    edge.player_y_q16 = -100 * HMBTN_Q16_ONE;
    edge = hmbtn_game_step(&edge, 0);
    assert(hmbtn_game_player_x(&edge) == 0);
    assert(hmbtn_game_player_y(&edge) == 0);

    const HmbtnCamera camera = hmbtn_camera_follow(&initial);
    assert(camera.width == HMBTN_LOGICAL_WIDTH);
    assert(camera.height == HMBTN_LOGICAL_HEIGHT);
    assert(camera.x >= 0 && camera.y >= 0);

    HmbtnGameState farmhouse = initial;
    farmhouse.player_x_q16 = 666 * HMBTN_Q16_ONE;
    farmhouse.player_y_q16 = 320 * HMBTN_Q16_ONE;
    farmhouse = hmbtn_game_step(&farmhouse, &right);
    assert(hmbtn_game_player_x(&farmhouse) == 668);
}

static void test_cheat_menu_uses_l3_r3_chord_only(void) {
    const HmbtnInputState circle = {
        .held = HMBTN_BUTTON_CIRCLE,
        .pressed = HMBTN_BUTTON_CIRCLE,
    };
    const HmbtnInputState l3_only = {
        .held = HMBTN_BUTTON_L3,
        .pressed = HMBTN_BUTTON_L3,
    };
    const HmbtnInputState chord = {
        .held = HMBTN_BUTTON_L3 | HMBTN_BUTTON_R3,
        .pressed = HMBTN_BUTTON_R3,
    };

    assert(!hmbtn_cheat_menu_requested(&circle));
    assert(!hmbtn_cheat_menu_requested(&l3_only));
    assert(hmbtn_cheat_menu_requested(&chord));
}

static void test_argb_to_ps4_abgr_conversion(void) {
    assert(hmbtn_argb_to_abgr(0xFF112233u) == 0xFF332211u);
    assert(hmbtn_argb_to_abgr(0x8040A0F0u) == 0x80F0A040u);
}

static void test_opaque_and_transparent_blending(void) {
    const uint32_t background = 0xFF102030u;

    assert(hmbtn_blend_argb_over_abgr(0xFFFF0000u, background) == 0xFF0000FFu);
    assert(hmbtn_blend_argb_over_abgr(0x00FFFFFFu, background) == background);
}

int main(void) {
    test_performance_contract();
    test_sixteen_by_nine_viewport_fills_1080p();
    test_fixed_step_is_bounded();
    test_exact_sixty_hz_clock();
    test_gameplay_movement_collision_and_camera();
    test_cheat_menu_uses_l3_r3_chord_only();
    test_argb_to_ps4_abgr_conversion();
    test_opaque_and_transparent_blending();

    puts("native_core_test: PASS");
    return 0;
}
