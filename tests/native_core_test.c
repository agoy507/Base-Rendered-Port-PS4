#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "port_base/core.h"
#include "port_base/game.h"
#include "port_base/input.h"
#include "port_base/pixel.h"

static void test_performance_contract(void) {
    assert(PORT_BASE_OUTPUT_WIDTH == 1920u);
    assert(PORT_BASE_OUTPUT_HEIGHT == 1080u);
    assert(PORT_BASE_TARGET_FPS == 60u);
    assert(PORT_BASE_MINIMUM_FPS == 50u);
    assert(PORT_BASE_SIMULATION_STEP_US == 16667u);
    assert(PORT_BASE_MAX_FRAME_TIME_US == 20000u);
}

static void test_sixteen_by_nine_viewport_fills_1080p(void) {
    const PortBaseViewport viewport = port_base_viewport_fit(
        1280u, 720u, PORT_BASE_OUTPUT_WIDTH, PORT_BASE_OUTPUT_HEIGHT);

    assert(viewport.x == 0u);
    assert(viewport.y == 0u);
    assert(viewport.width == 1920u);
    assert(viewport.height == 1080u);
}

static void test_fixed_step_is_bounded(void) {
    PortBaseFrameClock clock = port_base_frame_clock_create();

    assert(port_base_frame_clock_advance(&clock, 16667u) == 1u);
    assert(port_base_frame_clock_advance(&clock, 33334u) == 2u);
    assert(port_base_frame_clock_advance(&clock, 1000000u) == PORT_BASE_MAX_CATCH_UP_STEPS);
    assert(clock.accumulator_us < PORT_BASE_SIMULATION_STEP_US);
}

static void test_exact_sixty_hz_clock(void) {
    PortBaseFixedClock clock = port_base_fixed_clock_create();
    uint32_t total_steps = 0u;
    for (uint32_t index = 0u; index < 100u; ++index) {
        const PortBaseFixedClockAdvance advance =
            port_base_fixed_clock_advance(clock, 10000u);
        clock = advance.clock;
        total_steps += advance.steps;
    }
    assert(total_steps == 60u);
}

static void test_gameplay_movement_collision_and_camera(void) {
    const PortBaseGameState initial = port_base_game_create();
    const PortBaseInputState right = {.held = PORT_BASE_BUTTON_RIGHT};
    PortBaseGameState moved = initial;
    for (uint32_t tick = 0u; tick < 60u; ++tick) {
        moved = port_base_game_step(&moved, &right);
    }
    assert(port_base_game_player_x(&moved) == port_base_game_player_x(&initial) + 240);
    assert(port_base_game_player_y(&moved) == port_base_game_player_y(&initial));
    assert(moved.tick == 60u);

    const PortBaseInputState circle = {
        .held = PORT_BASE_BUTTON_CIRCLE,
        .pressed = PORT_BASE_BUTTON_CIRCLE,
    };
    const PortBaseGameState after_circle = port_base_game_step(&initial, &circle);
    assert(port_base_game_player_x(&after_circle) == port_base_game_player_x(&initial));
    assert(port_base_game_player_y(&after_circle) == port_base_game_player_y(&initial));

    PortBaseGameState edge = initial;
    edge.player_x_q16 = -100 * PORT_BASE_Q16_ONE;
    edge.player_y_q16 = -100 * PORT_BASE_Q16_ONE;
    edge = port_base_game_step(&edge, 0);
    assert(port_base_game_player_x(&edge) == 0);
    assert(port_base_game_player_y(&edge) == 0);

    const PortBaseCamera camera = port_base_camera_follow(&initial);
    assert(camera.width == PORT_BASE_LOGICAL_WIDTH);
    assert(camera.height == PORT_BASE_LOGICAL_HEIGHT);
    assert(camera.x >= 0 && camera.y >= 0);

    PortBaseGameState farmhouse = initial;
    farmhouse.player_x_q16 = 666 * PORT_BASE_Q16_ONE;
    farmhouse.player_y_q16 = 320 * PORT_BASE_Q16_ONE;
    farmhouse = port_base_game_step(&farmhouse, &right);
    assert(port_base_game_player_x(&farmhouse) == 668);
}

static void test_cheat_menu_uses_l3_r3_chord_only(void) {
    const PortBaseInputState circle = {
        .held = PORT_BASE_BUTTON_CIRCLE,
        .pressed = PORT_BASE_BUTTON_CIRCLE,
    };
    const PortBaseInputState l3_only = {
        .held = PORT_BASE_BUTTON_L3,
        .pressed = PORT_BASE_BUTTON_L3,
    };
    const PortBaseInputState chord = {
        .held = PORT_BASE_BUTTON_L3 | PORT_BASE_BUTTON_R3,
        .pressed = PORT_BASE_BUTTON_R3,
    };

    assert(!port_base_cheat_menu_requested(&circle));
    assert(!port_base_cheat_menu_requested(&l3_only));
    assert(port_base_cheat_menu_requested(&chord));
}

static void test_argb_to_ps4_abgr_conversion(void) {
    assert(port_base_argb_to_abgr(0xFF112233u) == 0xFF332211u);
    assert(port_base_argb_to_abgr(0x8040A0F0u) == 0x80F0A040u);
}

static void test_opaque_and_transparent_blending(void) {
    const uint32_t background = 0xFF102030u;

    assert(port_base_blend_argb_over_abgr(0xFFFF0000u, background) == 0xFF0000FFu);
    assert(port_base_blend_argb_over_abgr(0x00FFFFFFu, background) == background);
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
