#ifndef PORT_BASE_CORE_H
#define PORT_BASE_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_BASE_OUTPUT_WIDTH 1920u
#define PORT_BASE_OUTPUT_HEIGHT 1080u
#define PORT_BASE_TARGET_FPS 60u
#define PORT_BASE_MINIMUM_FPS 50u
#define PORT_BASE_SIMULATION_STEP_US 16667u
#define PORT_BASE_MAX_FRAME_TIME_US 20000u
#define PORT_BASE_MAX_CATCH_UP_STEPS 4u

typedef struct PortBaseViewport {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} PortBaseViewport;

typedef struct PortBaseFrameClock {
    uint64_t accumulator_us;
} PortBaseFrameClock;

typedef struct PortBaseFixedClock {
    uint64_t scaled_remainder;
} PortBaseFixedClock;

typedef struct PortBaseFixedClockAdvance {
    PortBaseFixedClock clock;
    uint32_t steps;
} PortBaseFixedClockAdvance;

PortBaseViewport port_base_viewport_fit(
    uint32_t content_width,
    uint32_t content_height,
    uint32_t output_width,
    uint32_t output_height);

PortBaseFrameClock port_base_frame_clock_create(void);
uint32_t port_base_frame_clock_advance(PortBaseFrameClock *clock, uint64_t delta_us);
PortBaseFixedClock port_base_fixed_clock_create(void);
PortBaseFixedClockAdvance port_base_fixed_clock_advance(
    PortBaseFixedClock clock,
    uint64_t delta_us);

#ifdef __cplusplus
}
#endif

#endif
