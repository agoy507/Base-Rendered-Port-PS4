#ifndef PORT_BASE_GAME_H
#define PORT_BASE_GAME_H

#include <stdint.h>

#include "port_base/input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_BASE_Q16_ONE 65536
#define PORT_BASE_LOGICAL_WIDTH 960
#define PORT_BASE_LOGICAL_HEIGHT 540
#define PORT_BASE_WORLD_WIDTH 1600
#define PORT_BASE_WORLD_HEIGHT 900
#define PORT_BASE_PLAYER_WIDTH 32
#define PORT_BASE_PLAYER_HEIGHT 48
#define PORT_BASE_PLAYER_SPEED_PER_TICK 4

typedef enum PortBaseFacing {
    PORT_BASE_FACING_DOWN = 0,
    PORT_BASE_FACING_LEFT = 1,
    PORT_BASE_FACING_RIGHT = 2,
    PORT_BASE_FACING_UP = 3
} PortBaseFacing;

typedef struct PortBaseGameState {
    int32_t player_x_q16;
    int32_t player_y_q16;
    PortBaseFacing facing;
    uint64_t tick;
} PortBaseGameState;

typedef struct PortBaseCamera {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PortBaseCamera;

PortBaseGameState port_base_game_create(void);
PortBaseGameState port_base_game_step(
    const PortBaseGameState *state,
    const PortBaseInputState *input);
PortBaseCamera port_base_camera_follow(const PortBaseGameState *state);
int32_t port_base_game_player_x(const PortBaseGameState *state);
int32_t port_base_game_player_y(const PortBaseGameState *state);

#ifdef __cplusplus
}
#endif

#endif
