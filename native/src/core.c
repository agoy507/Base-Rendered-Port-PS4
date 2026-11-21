#include "hmbtn/core.h"

HmbtnViewport hmbtn_viewport_fit(
    uint32_t content_width,
    uint32_t content_height,
    uint32_t output_width,
    uint32_t output_height) {
    HmbtnViewport viewport = {0u, 0u, 0u, 0u};
    if (content_width == 0u || content_height == 0u ||
        output_width == 0u || output_height == 0u) {
        return viewport;
    }

    const uint64_t width_limited_height =
        ((uint64_t)output_width * content_height) / content_width;
    if (width_limited_height <= output_height) {
        viewport.width = output_width;
        viewport.height = (uint32_t)width_limited_height;
    } else {
        viewport.height = output_height;
        viewport.width = (uint32_t)(
            ((uint64_t)output_height * content_width) / content_height);
    }

    viewport.x = (output_width - viewport.width) / 2u;
    viewport.y = (output_height - viewport.height) / 2u;
    return viewport;
}

HmbtnFrameClock hmbtn_frame_clock_create(void) {
    const HmbtnFrameClock clock = {0u};
    return clock;
}

uint32_t hmbtn_frame_clock_advance(HmbtnFrameClock *clock, uint64_t delta_us) {
    if (clock == 0) {
        return 0u;
    }

    const uint64_t maximum_accumulator =
        (uint64_t)HMBTN_SIMULATION_STEP_US * HMBTN_MAX_CATCH_UP_STEPS;
    const uint64_t available = clock->accumulator_us + delta_us;
    clock->accumulator_us =
        available < maximum_accumulator ? available : maximum_accumulator;

    const uint32_t steps =
        (uint32_t)(clock->accumulator_us / HMBTN_SIMULATION_STEP_US);
    clock->accumulator_us -= (uint64_t)steps * HMBTN_SIMULATION_STEP_US;
    return steps;
}

HmbtnFixedClock hmbtn_fixed_clock_create(void) {
    const HmbtnFixedClock clock = {0u};
    return clock;
}

HmbtnFixedClockAdvance hmbtn_fixed_clock_advance(
    HmbtnFixedClock clock,
    uint64_t delta_us) {
    const uint64_t scaled =
        clock.scaled_remainder + delta_us * (uint64_t)HMBTN_TARGET_FPS;
    const uint64_t available_steps = scaled / 1000000u;
    HmbtnFixedClockAdvance advance = {
        .clock = {.scaled_remainder = scaled % 1000000u},
        .steps = available_steps < HMBTN_MAX_CATCH_UP_STEPS
            ? (uint32_t)available_steps
            : HMBTN_MAX_CATCH_UP_STEPS,
    };
    return advance;
}
