#include "hmbtn/game.h"

typedef struct HmbtnRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} HmbtnRect;

static const HmbtnRect HMBTN_FARMHOUSE = {700, 300, 180, 120};

static int32_t hmbtn_clamp_i32(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

static int32_t hmbtn_q16_floor(int32_t value) {
    return value / HMBTN_Q16_ONE;
}

static int hmbtn_rects_intersect(HmbtnRect left, HmbtnRect right) {
    return left.x < right.x + right.width &&
           left.x + left.width > right.x &&
           left.y < right.y + right.height &&
           left.y + left.height > right.y;
}

static HmbtnRect hmbtn_player_rect(const HmbtnGameState *state) {
    const HmbtnRect rect = {
        hmbtn_q16_floor(state->player_x_q16),
        hmbtn_q16_floor(state->player_y_q16),
        HMBTN_PLAYER_WIDTH,
        HMBTN_PLAYER_HEIGHT,
    };
    return rect;
}

static HmbtnGameState hmbtn_move_x(HmbtnGameState state, int32_t delta_q16) {
    state.player_x_q16 += delta_q16;
    HmbtnRect player = hmbtn_player_rect(&state);
    if (hmbtn_rects_intersect(player, HMBTN_FARMHOUSE)) {
        const int32_t edge = delta_q16 > 0
            ? HMBTN_FARMHOUSE.x - HMBTN_PLAYER_WIDTH
            : HMBTN_FARMHOUSE.x + HMBTN_FARMHOUSE.width;
        state.player_x_q16 = edge * HMBTN_Q16_ONE;
    }
    return state;
}

static HmbtnGameState hmbtn_move_y(HmbtnGameState state, int32_t delta_q16) {
    state.player_y_q16 += delta_q16;
    HmbtnRect player = hmbtn_player_rect(&state);
    if (hmbtn_rects_intersect(player, HMBTN_FARMHOUSE)) {
        const int32_t edge = delta_q16 > 0
            ? HMBTN_FARMHOUSE.y - HMBTN_PLAYER_HEIGHT
            : HMBTN_FARMHOUSE.y + HMBTN_FARMHOUSE.height;
        state.player_y_q16 = edge * HMBTN_Q16_ONE;
    }
    return state;
}

static HmbtnGameState hmbtn_clamp_player(HmbtnGameState state) {
    state.player_x_q16 = hmbtn_clamp_i32(
        state.player_x_q16,
        0,
        (HMBTN_WORLD_WIDTH - HMBTN_PLAYER_WIDTH) * HMBTN_Q16_ONE);
    state.player_y_q16 = hmbtn_clamp_i32(
        state.player_y_q16,
        0,
        (HMBTN_WORLD_HEIGHT - HMBTN_PLAYER_HEIGHT) * HMBTN_Q16_ONE);
    return state;
}

HmbtnGameState hmbtn_game_create(void) {
    const HmbtnGameState state = {
        .player_x_q16 = 480 * HMBTN_Q16_ONE,
        .player_y_q16 = 520 * HMBTN_Q16_ONE,
        .facing = HMBTN_FACING_DOWN,
        .tick = 0u,
    };
    return state;
}

HmbtnGameState hmbtn_game_step(
    const HmbtnGameState *state,
    const HmbtnInputState *input) {
    HmbtnGameState next = state != 0 ? *state : hmbtn_game_create();
    int32_t axis_x = 0;
    int32_t axis_y = 0;
    if (input != 0) {
        axis_x = ((input->held & HMBTN_BUTTON_RIGHT) != 0u) -
                 ((input->held & HMBTN_BUTTON_LEFT) != 0u);
        axis_y = ((input->held & HMBTN_BUTTON_DOWN) != 0u) -
                 ((input->held & HMBTN_BUTTON_UP) != 0u);
    }

    int32_t speed_q16 = HMBTN_PLAYER_SPEED_PER_TICK * HMBTN_Q16_ONE;
    if (axis_x != 0 && axis_y != 0) {
        speed_q16 = (int32_t)(((int64_t)speed_q16 * 46341) / HMBTN_Q16_ONE);
    }
    if (axis_x != 0) {
        next.facing = axis_x < 0 ? HMBTN_FACING_LEFT : HMBTN_FACING_RIGHT;
        next = hmbtn_move_x(next, axis_x * speed_q16);
    }
    if (axis_y != 0) {
        next.facing = axis_y < 0 ? HMBTN_FACING_UP : HMBTN_FACING_DOWN;
        next = hmbtn_move_y(next, axis_y * speed_q16);
    }
    next = hmbtn_clamp_player(next);
    next.tick += 1u;
    return next;
}

HmbtnCamera hmbtn_camera_follow(const HmbtnGameState *state) {
    const HmbtnGameState fallback = hmbtn_game_create();
    const HmbtnGameState *current = state != 0 ? state : &fallback;
    const int32_t player_center_x =
        hmbtn_game_player_x(current) + HMBTN_PLAYER_WIDTH / 2;
    const int32_t player_center_y =
        hmbtn_game_player_y(current) + HMBTN_PLAYER_HEIGHT / 2;
    const HmbtnCamera camera = {
        .x = hmbtn_clamp_i32(
            player_center_x - HMBTN_LOGICAL_WIDTH / 2,
            0,
            HMBTN_WORLD_WIDTH - HMBTN_LOGICAL_WIDTH),
        .y = hmbtn_clamp_i32(
            player_center_y - HMBTN_LOGICAL_HEIGHT / 2,
            0,
            HMBTN_WORLD_HEIGHT - HMBTN_LOGICAL_HEIGHT),
        .width = HMBTN_LOGICAL_WIDTH,
        .height = HMBTN_LOGICAL_HEIGHT,
    };
    return camera;
}

int32_t hmbtn_game_player_x(const HmbtnGameState *state) {
    return state != 0 ? hmbtn_q16_floor(state->player_x_q16) : 0;
}

int32_t hmbtn_game_player_y(const HmbtnGameState *state) {
    return state != 0 ? hmbtn_q16_floor(state->player_y_q16) : 0;
}
