#ifndef HMBTN_CORE_H
#define HMBTN_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMBTN_OUTPUT_WIDTH 1920u
#define HMBTN_OUTPUT_HEIGHT 1080u
#define HMBTN_TARGET_FPS 60u
#define HMBTN_MINIMUM_FPS 50u
#define HMBTN_SIMULATION_STEP_US 16667u
#define HMBTN_MAX_FRAME_TIME_US 20000u
#define HMBTN_MAX_CATCH_UP_STEPS 4u

typedef struct HmbtnViewport {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} HmbtnViewport;

typedef struct HmbtnFrameClock {
    uint64_t accumulator_us;
} HmbtnFrameClock;

typedef struct HmbtnFixedClock {
    uint64_t scaled_remainder;
} HmbtnFixedClock;

typedef struct HmbtnFixedClockAdvance {
    HmbtnFixedClock clock;
    uint32_t steps;
} HmbtnFixedClockAdvance;

HmbtnViewport hmbtn_viewport_fit(
    uint32_t content_width,
    uint32_t content_height,
    uint32_t output_width,
    uint32_t output_height);

HmbtnFrameClock hmbtn_frame_clock_create(void);
uint32_t hmbtn_frame_clock_advance(HmbtnFrameClock *clock, uint64_t delta_us);
HmbtnFixedClock hmbtn_fixed_clock_create(void);
HmbtnFixedClockAdvance hmbtn_fixed_clock_advance(
    HmbtnFixedClock clock,
    uint64_t delta_us);

#ifdef __cplusplus
}
#endif

#endif
