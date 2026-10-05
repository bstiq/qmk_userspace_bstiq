// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Sand: grains fall along the motion (snapped to eight directions), sliding
 * 45 degrees either side when blocked, so they pile into slopes. The pile is
 * kept between animations. Each step visits grains nearest the "floor" first,
 * so a column falls together instead of one cell at a time.
 */

static uint8_t bklm_sand[BKLM_ROWS][BKLM_COLS]; /* 0 empty, else hue + 1 */

static int8_t bklm_sand_sign(int16_t v) {
    return v > 48 ? 1 : (v < -48 ? -1 : 0);
}

static bool bklm_sand_free(int8_t x, int8_t y) {
    return x >= 0 && x < BKLM_COLS && y >= 0 && y < BKLM_ROWS && bklm_sand[y][x] == 0;
}

static void bklm_sand_step(int8_t gx, int8_t gy) {
    static uint8_t moved[BKLM_ROWS][BKLM_COLS];
    memset(moved, 0, sizeof(moved));

    /* Positions along gravity run from -(COLS + ROWS) to +(COLS + ROWS);
     * visit the largest (nearest the floor) first. */
    const int16_t span = BKLM_COLS + BKLM_ROWS;
    for (int16_t level = span; level >= -span; level--) {
        for (int8_t y = 0; y < BKLM_ROWS; y++) {
            for (int8_t x = 0; x < BKLM_COLS; x++) {
                if (x * gx + y * gy != level || bklm_sand[y][x] == 0 || moved[y][x]) continue;
                /* Straight down, else the two 45-degree slides in random order. */
                int8_t ax = (int8_t)CONSTRAIN(gx - gy, -1, 1), ay = (int8_t)CONSTRAIN(gy + gx, -1, 1);
                int8_t bx = (int8_t)CONSTRAIN(gx + gy, -1, 1), by = (int8_t)CONSTRAIN(gy - gx, -1, 1);
                if (bklm_rand() & 1) {
                    int8_t t = ax; ax = bx; bx = t;
                    t = ay; ay = by; by = t;
                }
                const int8_t opts[3][2] = {{gx, gy}, {ax, ay}, {bx, by}};
                for (uint8_t o = 0; o < 3; o++) {
                    const int8_t nx = (int8_t)(x + opts[o][0]), ny = (int8_t)(y + opts[o][1]);
                    if ((opts[o][0] || opts[o][1]) && bklm_sand_free(nx, ny)) {
                        bklm_sand[ny][nx] = bklm_sand[y][x];
                        bklm_sand[y][x]   = 0;
                        moved[ny][nx]     = 1;
                        break;
                    }
                }
            }
        }
    }
}

bool bklm_motion_sand(RGB *pixels, const bklm_motion_t *m) {
    static bool    filled;
    static int8_t  gx = 0, gy = -1; /* gravity, starts down */
    static int32_t due;             /* ms of simulation owed */

    if (!filled) {
        for (uint8_t y = 0; y < LED_MATRIX_MODULE_SAND_ROWS; y++) {
            for (uint8_t x = 0; x < BKLM_COLS; x++) {
                bklm_sand[y][x] = (uint8_t)(1 + (y * 240 / LED_MATRIX_MODULE_SAND_ROWS + x * 3) % 255); /* never 0: that is empty */
            }
        }
        filled = true;
    }

    const bool moving = m->still_ms < 60;
    if (moving) {
        const int8_t sx = bklm_sand_sign(m->dir_x), sy = bklm_sand_sign(m->dir_y);
        if (sx || sy) {
            gx = sx;
            gy = sy;
        }
        bklm_motion_linger(LED_MATRIX_MODULE_SAND_LINGER_MS);
    }

    /* One step every LED_MATRIX_MODULE_SAND_STEP_MS, faster when rolling hard. */
    due += (int32_t)m->dt * (moving ? 1 + m->norm / 128 : 1);
    for (uint8_t n = 0; due >= LED_MATRIX_MODULE_SAND_STEP_MS && n < 4; n++) {
        due -= LED_MATRIX_MODULE_SAND_STEP_MS;
        bklm_sand_step(gx, gy);
    }
    due = MIN(due, LED_MATRIX_MODULE_SAND_STEP_MS);

    for (uint8_t y = 0; y < BKLM_ROWS; y++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            if (bklm_sand[y][x]) {
                *bklm_px(pixels, x, y) = hsv_to_rgb((HSV){.h = (uint8_t)(bklm_sand[y][x] - 1), .s = 220, .v = (uint8_t)(220 * (uint32_t)m->fade / 255)});
            }
        }
    }
    return true;
}
