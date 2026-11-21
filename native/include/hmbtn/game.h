#ifndef HMBTN_GAME_H
#define HMBTN_GAME_H

#include <stdint.h>

#include "hmbtn/input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMBTN_Q16_ONE 65536
#define HMBTN_LOGICAL_WIDTH 960
#define HMBTN_LOGICAL_HEIGHT 540
#define HMBTN_WORLD_WIDTH 1600
#define HMBTN_WORLD_HEIGHT 900
#define HMBTN_PLAYER_WIDTH 32
#define HMBTN_PLAYER_HEIGHT 48
#define HMBTN_PLAYER_SPEED_PER_TICK 4

typedef enum HmbtnFacing {
    HMBTN_FACING_DOWN = 0,
    HMBTN_FACING_LEFT = 1,
    HMBTN_FACING_RIGHT = 2,
    HMBTN_FACING_UP = 3
} HmbtnFacing;

typedef struct HmbtnGameState {
    int32_t player_x_q16;
    int32_t player_y_q16;
    HmbtnFacing facing;
    uint64_t tick;
} HmbtnGameState;

typedef struct HmbtnCamera {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} HmbtnCamera;

HmbtnGameState hmbtn_game_create(void);
HmbtnGameState hmbtn_game_step(
    const HmbtnGameState *state,
    const HmbtnInputState *input);
HmbtnCamera hmbtn_camera_follow(const HmbtnGameState *state);
int32_t hmbtn_game_player_x(const HmbtnGameState *state);
int32_t hmbtn_game_player_y(const HmbtnGameState *state);

#ifdef __cplusplus
}
#endif

#endif
