// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_motion.h"

/*
 * Motion core. Velocity is measured per frame from the motion fed in since the
 * previous frame and smoothed, so the direction does not jitter between frames.
 * The last good direction is kept while the animation fades, so it does not
 * swing around as the velocity decays to zero.
 */

uint8_t bklm_motion_style = LED_MATRIX_MODULE_MOTION_STYLE;

static const char *const bklm_motion_names[BKLM_MOTION_STYLE_COUNT] = {
    [BKLM_MOTION_WAVE] = "wave", [BKLM_MOTION_CHEVRON] = "chevron", [BKLM_MOTION_COMET] = "comet",
    [BKLM_MOTION_SPARKS] = "sparks", [BKLM_MOTION_STARS] = "stars", [BKLM_MOTION_BALL] = "ball",
    [BKLM_MOTION_WARP] = "warp", [BKLM_MOTION_FIREWORKS] = "fireworks", [BKLM_MOTION_FIREFLIES] = "fireflies",
    [BKLM_MOTION_SAND] = "sand", [BKLM_MOTION_OCEAN] = "ocean", [BKLM_MOTION_WATER] = "water", [BKLM_MOTION_CAR] = "car", [BKLM_MOTION_ASTEROIDS] = "asteroids", [BKLM_MOTION_MATRIX] = "matrix", [BKLM_MOTION_TETRIS] = "tetris",
    [BKLM_MOTION_BADAPPLE] = "badapple",
};

static const uint8_t bklm_motion_cycle[] = {LED_MATRIX_MODULE_MOTION_CYCLE};

uint8_t bklm_motion_cycle_next(uint8_t style, bool backwards) {
    const uint8_t n = sizeof(bklm_motion_cycle);
    uint8_t       i = 0;
    while (i < n && bklm_motion_cycle[i] != style) i++;
    if (i == n) {
        return bklm_motion_cycle[0]; /* not in the cycle: start it */
    }
    return bklm_motion_cycle[(i + (backwards ? n - 1 : 1)) % n];
}

const char *bklm_motion_style_name(uint8_t style) {
    return style < BKLM_MOTION_STYLE_COUNT ? bklm_motion_names[style] : "?";
}

void bklm_motion_style_load(void) {
    const uint8_t saved = (uint8_t)(eeconfig_read_user() & 0xFF);
    if (saved >= 1 && saved <= BKLM_MOTION_STYLE_COUNT) {
        bklm_motion_style = saved - 1;
    }
}

static uint32_t bklm_preview_until;
static uint32_t bklm_linger_until;

void bklm_motion_linger(uint32_t ms) {
    const uint32_t until = timer_read32() + ms;
    if ((int32_t)(until - bklm_linger_until) > 0) {
        bklm_linger_until = until;
    }
}

static int32_t  bklm_pending_x, bklm_pending_y; /* report coords, since the last frame */
static int32_t  bklm_vel_x, bklm_vel_y;         /* report coords, counts per second */
static int16_t  bklm_dir_x = 127, bklm_dir_y;   /* panel coords */
static uint32_t bklm_last_motion_ms, bklm_last_frame_ms;
static bool     bklm_seen, bklm_was_active;

void bklm_motion_feed(int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    bklm_pending_x += dx;
    bklm_pending_y += dy;
    bklm_last_motion_ms = timer_read32();
    bklm_seen           = true;
}

void bklm_motion_style_set(uint8_t style, bool preview) {
    if (style >= BKLM_MOTION_STYLE_COUNT) {
        return;
    }
    if (style != bklm_motion_style) {
        eeconfig_update_user((eeconfig_read_user() & ~0xFFUL) | (uint32_t)(style + 1));
    }
    bklm_motion_style = style;
    if (!preview) {
        return;
    }
    /* Preview: pretend the ball rolls up and to the right for a moment. The
     * style restarts from scratch, as it would after a pause. */
    bklm_preview_until  = timer_read32() + LED_MATRIX_MODULE_MOTION_PREVIEW_MS;
    bklm_last_motion_ms = timer_read32();
    bklm_seen           = true;
    bklm_was_active     = false;
}

static bool bklm_moving_window(void) {
    return bklm_seen && timer_elapsed32(bklm_last_motion_ms) < LED_MATRIX_MODULE_MOTION_HOLD_MS + LED_MATRIX_MODULE_MOTION_FADE_MS;
}

static bool bklm_lingering(void) {
    return bklm_seen && (int32_t)(bklm_linger_until - timer_read32()) > 0;
}

bool bklm_motion_moving(void) {
    return bklm_moving_window() || bklm_lingering();
}

bool bklm_motion_active(void) {
    return LED_MATRIX_MODULE_MOTION_IDLE || bklm_motion_moving();
}

uint32_t bklm_isqrt(uint32_t n) {
    uint32_t root = 0;
    for (uint32_t bit = 1UL << 30; bit != 0; bit >>= 2) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

uint32_t bklm_rand(void) {
    static uint32_t s = 0x9E3779B9;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int32_t bklm_motion_step(int32_t velocity, uint32_t dt, uint32_t counts_per_cell) {
    return (int32_t)((int64_t)velocity * dt * 256 / (1000 * (int64_t)counts_per_cell));
}

static int32_t bklm_wrap(int32_t v, int32_t size) {
    v %= size;
    return v < 0 ? v + size : v;
}

/* sin over a quarter turn, 0..64 -> 0..127. */
static const int8_t bklm_sin_q[65] = {
    0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,  49,  51,  54,  57,  60,  63,
    65,  68,  71,  73,  76,  78,  81,  83,  85,  88,  90,  92,  94,  96,  98,  100, 102, 104, 106, 107, 109, 111,
    112, 113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127,
};

int32_t bklm_sin8(uint8_t a) {
    const uint8_t q = a & 63;
    switch (a >> 6) {
        case 0:
            return bklm_sin_q[q];
        case 1:
            return bklm_sin_q[64 - q];
        case 2:
            return -bklm_sin_q[q];
        default:
            return -bklm_sin_q[64 - q];
    }
}

int32_t bklm_cos8(uint8_t a) {
    return bklm_sin8((uint8_t)(a + 64));
}

/* Angle of (x, y), 0..255. atan on the first octant from a ratio, then
 * mirrored into place; plenty for steering things on a 12x16 panel. */
uint8_t bklm_atan2_8(int32_t y, int32_t x) {
    const int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax == 0 && ay == 0) return 0;
    /* atan(t) for t = 0..1 in 32 steps, in angle units (32 = 45 degrees). */
    static const uint8_t atan_t[33] = {0,  1,  3,  4,  5,  6,  8,  9,  10, 11, 12, 13, 15, 16, 17, 18, 19,
                                       20, 21, 22, 23, 24, 25, 25, 26, 27, 28, 29, 29, 30, 31, 31, 32};
    uint8_t              a = ax >= ay ? atan_t[ay * 32 / ax] : (uint8_t)(64 - atan_t[ax * 32 / ay]);
    if (x < 0) a = (uint8_t)(128 - a);
    if (y < 0) a = (uint8_t)(256 - a);
    return a;
}

void bklm_splat(RGB *pixels, int32_t x_q8, int32_t y_q8, RGB c, uint8_t level, bool wrap) {
    /* Cell centres sit at integer + 0.5, so shift by half a cell first. */
    const int32_t  fx = x_q8 - 128, fy = y_q8 - 128;
    const int32_t  x0 = fx >> 8, y0 = fy >> 8; /* floor, also for negatives */
    const uint32_t ax = (uint32_t)(fx & 255), ay = (uint32_t)(fy & 255);
    const uint32_t w[4] = {(256 - ax) * (256 - ay), ax * (256 - ay), (256 - ax) * ay, ax * ay};
    for (uint8_t i = 0; i < 4; i++) {
        int32_t x = x0 + (i & 1), y = y0 + (i >> 1);
        if (wrap) {
            x = bklm_wrap(x, BKLM_COLS);
            y = bklm_wrap(y, BKLM_ROWS);
        } else if (x < 0 || x >= BKLM_COLS || y < 0 || y >= BKLM_ROWS) {
            continue;
        }
        bklm_add(bklm_px(pixels, (uint8_t)x, (uint8_t)y), c, (uint8_t)(level * w[i] >> 16));
    }
}

bool bklm_draw_motion(RGB *pixels) {
    const uint32_t now = timer_read32();
    uint32_t       dt  = now - bklm_last_frame_ms;
    bklm_last_frame_ms = now;
    if (dt == 0) {
        dt = 1;
    } else if (dt > 200) {
        dt = 200; /* first frame after a pause: don't let anything jump */
    }

    /* While previewing a newly picked style, roll the virtual ball. */
    if ((int32_t)(bklm_preview_until - now) > 0) {
        bklm_pending_x += (int32_t)(2000 * dt / 1000);
        bklm_pending_y -= (int32_t)(1000 * dt / 1000); /* report y is down: this is up */
        bklm_last_motion_ms = now;
    }

    /* Idle: the ball has been still past the fade and nothing lingers. Instead
     * of stopping, ease towards a slow drift along the last direction. */
    const bool idle = LED_MATRIX_MODULE_MOTION_IDLE && !bklm_motion_moving();
    if (idle) {
        const int32_t drift_x = bklm_dir_x * LED_MATRIX_MODULE_MOTION_IDLE_SPEED / 127;
        const int32_t drift_y = -bklm_dir_y * LED_MATRIX_MODULE_MOTION_IDLE_SPEED / 127; /* report coords: y down */
        bklm_vel_x += (drift_x - bklm_vel_x) / 8;
        bklm_vel_y += (drift_y - bklm_vel_y) / 8;
    } else {
        /* Motion since the last frame as a rate, then halfway towards it. */
        bklm_vel_x += (bklm_pending_x * 1000 / (int32_t)dt - bklm_vel_x) / 2;
        bklm_vel_y += (bklm_pending_y * 1000 / (int32_t)dt - bklm_vel_y) / 2;
    }
    bklm_pending_x = 0;
    bklm_pending_y = 0;

    if (!bklm_motion_active()) {
        bklm_vel_x      = 0;
        bklm_vel_y      = 0;
        bklm_was_active = false;
        return false;
    }

    /* Clamp before squaring so the sum fits in 32 bits. */
    const int32_t  vx    = CONSTRAIN(bklm_vel_x, -30000, 30000);
    const int32_t  vy    = CONSTRAIN(-bklm_vel_y, -30000, 30000); /* panel y is up */
    const uint32_t speed = bklm_isqrt((uint32_t)(vx * vx) + (uint32_t)(vy * vy));
    if (speed >= LED_MATRIX_MODULE_MOTION_MIN_SPEED) {
        bklm_dir_x = (int16_t)(vx * 127 / (int32_t)speed);
        bklm_dir_y = (int16_t)(vy * 127 / (int32_t)speed);
    }

    const uint32_t still = bklm_seen ? timer_elapsed32(bklm_last_motion_ms) : UINT32_MAX;
    uint32_t       fade  = 255;
    if (!LED_MATRIX_MODULE_MOTION_IDLE && still > LED_MATRIX_MODULE_MOTION_HOLD_MS && bklm_moving_window()) {
        fade = 255 * (LED_MATRIX_MODULE_MOTION_HOLD_MS + LED_MATRIX_MODULE_MOTION_FADE_MS - still) / LED_MATRIX_MODULE_MOTION_FADE_MS;
    }
    /* A lingering style fades over the end of its linger instead. */
    if (!LED_MATRIX_MODULE_MOTION_IDLE && bklm_lingering()) {
        const uint32_t left       = bklm_linger_until - now;
        const uint32_t fade_after = left >= LED_MATRIX_MODULE_MOTION_FADE_MS ? 255 : 255 * left / LED_MATRIX_MODULE_MOTION_FADE_MS;
        fade                      = MAX(bklm_moving_window() ? fade : 0, fade_after);
    }

    const bklm_motion_t m = {
        .vx       = vx,
        .vy       = vy,
        .dir_x    = bklm_dir_x,
        .dir_y    = bklm_dir_y,
        .speed    = speed,
        .norm     = (uint8_t)(255 * MIN(speed, LED_MATRIX_MODULE_MOTION_FULL_SPEED) / LED_MATRIX_MODULE_MOTION_FULL_SPEED),
        .fade     = (uint8_t)fade,
        .still_ms = still,
        .dt       = dt,
        .now      = now,
        .fresh    = !bklm_was_active,
        .idle     = idle,
    };
    bklm_was_active = true;

    switch (bklm_motion_style) {
        case BKLM_MOTION_CHEVRON:
            return bklm_motion_bands(pixels, &m, true);
        case BKLM_MOTION_COMET:
            return bklm_motion_comet(pixels, &m);
        case BKLM_MOTION_SPARKS:
            return bklm_motion_sparks(pixels, &m);
        case BKLM_MOTION_STARS:
            return bklm_motion_stars(pixels, &m);
        case BKLM_MOTION_BALL:
            return bklm_motion_ball(pixels, &m);
        case BKLM_MOTION_WARP:
            return bklm_motion_warp(pixels, &m);
        case BKLM_MOTION_FIREWORKS:
            return bklm_motion_fireworks(pixels, &m);
        case BKLM_MOTION_FIREFLIES:
            return bklm_motion_fireflies(pixels, &m);
        case BKLM_MOTION_SAND:
            return bklm_motion_sand(pixels, &m);
        case BKLM_MOTION_OCEAN:
            return bklm_motion_ocean(pixels, &m);
        case BKLM_MOTION_WATER:
            return bklm_motion_water(pixels, &m);
        case BKLM_MOTION_CAR:
            return bklm_motion_car(pixels, &m);
        case BKLM_MOTION_ASTEROIDS:
            return bklm_motion_asteroids(pixels, &m);
        case BKLM_MOTION_MATRIX:
            return bklm_motion_matrix(pixels, &m);
        case BKLM_MOTION_TETRIS:
            return bklm_motion_tetris(pixels, &m);
        case BKLM_MOTION_BADAPPLE:
            return bklm_motion_badapple(pixels, &m);
        case BKLM_MOTION_WAVE:
        default:
            return bklm_motion_bands(pixels, &m, false);
    }
}
