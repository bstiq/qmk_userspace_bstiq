// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Starfield: a wrapping field streaming opposite to the motion, in three
 * depths; near stars are brighter, faster, and streak at speed.
 */

#define BKLM_STARS 26
#define BKLM_STARS_W (BKLM_COLS * 256)
#define BKLM_STARS_H (BKLM_ROWS * 256)

typedef struct {
    int32_t x, y;  /* Q8 cells */
    uint8_t depth; /* 1 far .. 3 near */
} bklm_star_t;

static void bklm_stars_wrap(int32_t *v, int32_t size) {
    *v %= size;
    if (*v < 0) *v += size;
}

bool bklm_motion_stars(RGB *pixels, const bklm_motion_t *m) {
    static bklm_star_t stars[BKLM_STARS];
    static bool        placed;

    if (!placed) {
        for (uint8_t i = 0; i < BKLM_STARS; i++) {
            stars[i].x     = (int32_t)(bklm_rand() % BKLM_STARS_W);
            stars[i].y     = (int32_t)(bklm_rand() % BKLM_STARS_H);
            stars[i].depth = (uint8_t)(1 + i % 3);
        }
        placed = true;
    }

    /* Streak length in Q8 cells: none when slow, up to two cells at full speed. */
    const int32_t streak = 512 * (int32_t)m->norm / 255;

    for (uint8_t i = 0; i < BKLM_STARS; i++) {
        bklm_star_t  *s     = &stars[i];
        const int32_t depth = s->depth;
        s->x -= bklm_motion_step(m->vx * depth, m->dt, LED_MATRIX_MODULE_STARS_COUNTS_PER_CELL);
        s->y -= bklm_motion_step(m->vy * depth, m->dt, LED_MATRIX_MODULE_STARS_COUNTS_PER_CELL);
        bklm_stars_wrap(&s->x, BKLM_STARS_W);
        bklm_stars_wrap(&s->y, BKLM_STARS_H);

        static const uint8_t bright[4] = {0, 70, 150, 255};
        const uint8_t        level     = (uint8_t)(bright[depth] * (uint32_t)m->fade / 255);
        const RGB            c         = {.r = (uint8_t)(90 + 55 * depth), .g = (uint8_t)(130 + 41 * depth), .b = 255};
        bklm_splat(pixels, s->x, s->y, c, level, true);

        /* Towards +direction: the star moves the other way. */
        const int32_t len = streak * depth / 3;
        for (int32_t t = 128; t <= len; t += 128) {
            const uint8_t tail = (uint8_t)(level * (uint32_t)(len - t + 128) / (uint32_t)(len + 128) / 2);
            bklm_splat(pixels, s->x + m->dir_x * t / 127, s->y + m->dir_y * t / 127, c, tail, true);
        }
    }
    return true;
}
