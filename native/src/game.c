#include "port_base/game.h"

typedef struct PortBaseRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PortBaseRect;

static const PortBaseRect PORT_BASE_FARMHOUSE = {700, 300, 180, 120};

static int32_t port_base_clamp_i32(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

static int32_t port_base_q16_floor(int32_t value) {
    return value / PORT_BASE_Q16_ONE;
}

static int port_base_rects_intersect(PortBaseRect left, PortBaseRect right) {
    return left.x < right.x + right.width &&
           left.x + left.width > right.x &&
           left.y < right.y + right.height &&
           left.y + left.height > right.y;
}

static PortBaseRect port_base_player_rect(const PortBaseGameState *state) {
    const PortBaseRect rect = {
        port_base_q16_floor(state->player_x_q16),
        port_base_q16_floor(state->player_y_q16),
        PORT_BASE_PLAYER_WIDTH,
        PORT_BASE_PLAYER_HEIGHT,
    };
    return rect;
}

static PortBaseGameState port_base_move_x(PortBaseGameState state, int32_t delta_q16) {
    state.player_x_q16 += delta_q16;
    PortBaseRect player = port_base_player_rect(&state);
    if (port_base_rects_intersect(player, PORT_BASE_FARMHOUSE)) {
        const int32_t edge = delta_q16 > 0
            ? PORT_BASE_FARMHOUSE.x - PORT_BASE_PLAYER_WIDTH
            : PORT_BASE_FARMHOUSE.x + PORT_BASE_FARMHOUSE.width;
        state.player_x_q16 = edge * PORT_BASE_Q16_ONE;
    }
    return state;
}

static PortBaseGameState port_base_move_y(PortBaseGameState state, int32_t delta_q16) {
    state.player_y_q16 += delta_q16;
    PortBaseRect player = port_base_player_rect(&state);
    if (port_base_rects_intersect(player, PORT_BASE_FARMHOUSE)) {
        const int32_t edge = delta_q16 > 0
            ? PORT_BASE_FARMHOUSE.y - PORT_BASE_PLAYER_HEIGHT
            : PORT_BASE_FARMHOUSE.y + PORT_BASE_FARMHOUSE.height;
        state.player_y_q16 = edge * PORT_BASE_Q16_ONE;
    }
    return state;
}

static PortBaseGameState port_base_clamp_player(PortBaseGameState state) {
    state.player_x_q16 = port_base_clamp_i32(
        state.player_x_q16,
        0,
        (PORT_BASE_WORLD_WIDTH - PORT_BASE_PLAYER_WIDTH) * PORT_BASE_Q16_ONE);
    state.player_y_q16 = port_base_clamp_i32(
        state.player_y_q16,
        0,
        (PORT_BASE_WORLD_HEIGHT - PORT_BASE_PLAYER_HEIGHT) * PORT_BASE_Q16_ONE);
    return state;
}

PortBaseGameState port_base_game_create(void) {
    const PortBaseGameState state = {
        .player_x_q16 = 480 * PORT_BASE_Q16_ONE,
        .player_y_q16 = 520 * PORT_BASE_Q16_ONE,
        .facing = PORT_BASE_FACING_DOWN,
        .tick = 0u,
    };
    return state;
}

PortBaseGameState port_base_game_step(
    const PortBaseGameState *state,
    const PortBaseInputState *input) {
    PortBaseGameState next = state != 0 ? *state : port_base_game_create();
    int32_t axis_x = 0;
    int32_t axis_y = 0;
    if (input != 0) {
        axis_x = ((input->held & PORT_BASE_BUTTON_RIGHT) != 0u) -
                 ((input->held & PORT_BASE_BUTTON_LEFT) != 0u);
        axis_y = ((input->held & PORT_BASE_BUTTON_DOWN) != 0u) -
                 ((input->held & PORT_BASE_BUTTON_UP) != 0u);
    }

    int32_t speed_q16 = PORT_BASE_PLAYER_SPEED_PER_TICK * PORT_BASE_Q16_ONE;
    if (axis_x != 0 && axis_y != 0) {
        speed_q16 = (int32_t)(((int64_t)speed_q16 * 46341) / PORT_BASE_Q16_ONE);
    }
    if (axis_x != 0) {
        next.facing = axis_x < 0 ? PORT_BASE_FACING_LEFT : PORT_BASE_FACING_RIGHT;
        next = port_base_move_x(next, axis_x * speed_q16);
    }
    if (axis_y != 0) {
        next.facing = axis_y < 0 ? PORT_BASE_FACING_UP : PORT_BASE_FACING_DOWN;
        next = port_base_move_y(next, axis_y * speed_q16);
    }
    next = port_base_clamp_player(next);
    next.tick += 1u;
    return next;
}

PortBaseCamera port_base_camera_follow(const PortBaseGameState *state) {
    const PortBaseGameState fallback = port_base_game_create();
    const PortBaseGameState *current = state != 0 ? state : &fallback;
    const int32_t player_center_x =
        port_base_game_player_x(current) + PORT_BASE_PLAYER_WIDTH / 2;
    const int32_t player_center_y =
        port_base_game_player_y(current) + PORT_BASE_PLAYER_HEIGHT / 2;
    const PortBaseCamera camera = {
        .x = port_base_clamp_i32(
            player_center_x - PORT_BASE_LOGICAL_WIDTH / 2,
            0,
            PORT_BASE_WORLD_WIDTH - PORT_BASE_LOGICAL_WIDTH),
        .y = port_base_clamp_i32(
            player_center_y - PORT_BASE_LOGICAL_HEIGHT / 2,
            0,
            PORT_BASE_WORLD_HEIGHT - PORT_BASE_LOGICAL_HEIGHT),
        .width = PORT_BASE_LOGICAL_WIDTH,
        .height = PORT_BASE_LOGICAL_HEIGHT,
    };
    return camera;
}

int32_t port_base_game_player_x(const PortBaseGameState *state) {
    return state != 0 ? port_base_q16_floor(state->player_x_q16) : 0;
}

int32_t port_base_game_player_y(const PortBaseGameState *state) {
    return state != 0 ? port_base_q16_floor(state->player_y_q16) : 0;
}
