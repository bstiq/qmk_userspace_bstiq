// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Water: side view of a tank that tilts with sideways motion and sloshes back
 * when you stop. The surface is one height per column, moved by the damped 1-D
 * wave equation with walls at both ends, in small fixed time steps so it stays
 * stable.
 */

#define BKLM_WATER_STEP_MS 4
#define BKLM_WATER_SC 16 /* state is kept 16x finer than Q8, so slow sloshes don't round away */

bool bklm_motion_water(RGB *pixels, const bklm_motion_t *m) {
    static int32_t h[BKLM_COLS], v[BKLM_COLS]; /* surface height and its speed, Q8 cells (/s) * BKLM_WATER_SC */
    static int32_t due;
    const int32_t  rest = LED_MATRIX_MODULE_WATER_LEVEL * 256 * BKLM_WATER_SC;

    if (m->fresh) {
        for (uint8_t i = 0; i < BKLM_COLS; i++) {
            h[i] = rest;
            v[i] = 0;
        }
        due = 0;
    }
    const bool moving = m->still_ms < 60;
    if (moving) {
        bklm_motion_linger(LED_MATRIX_MODULE_WATER_LINGER_MS);
    }

    /* Sideways velocity as -255..255 of full speed: how hard the tank tilts.
     * Idle, it rocks gently instead, like a boat: a 4 s triangle, +-70. */
    int32_t tilt = moving ? CONSTRAIN(m->vx * 255 / (int32_t)LED_MATRIX_MODULE_MOTION_FULL_SPEED, -255, 255) : 0;
    if (m->idle) {
        const int32_t t = (int32_t)(m->now % 4000);
        tilt            = (t < 2000 ? t - 1000 : 3000 - t) * 70 / 1000;
    }

    due += (int32_t)m->dt;
    while (due >= BKLM_WATER_STEP_MS) {
        due -= BKLM_WATER_STEP_MS;
        int32_t a[BKLM_COLS];
        for (uint8_t i = 0; i < BKLM_COLS; i++) {
            const int32_t left  = h[i > 0 ? i - 1 : i];             /* walls: reflect */
            const int32_t right = h[i < BKLM_COLS - 1 ? i + 1 : i];
            a[i] = (left + right - 2 * h[i]) * 120                                 /* wave speed^2: ~11 cells/s */
                 + tilt * (2 * i - (BKLM_COLS - 1)) * 3 * BKLM_WATER_SC            /* tilt: heap up on the motion's side */
                 - v[i] * 3 / 2;                                                   /* damping */
            if (moving) { /* chop */
                a[i] += ((int32_t)(bklm_rand() % 801) - 400) * m->norm / 255 * 8 * BKLM_WATER_SC;
            } else if (m->idle) { /* idle: a gentle ripple, so the water is alive */
                a[i] += ((int32_t)(bklm_rand() % 801) - 400) * BKLM_WATER_SC;
            }
        }
        int32_t sum = 0;
        for (uint8_t i = 0; i < BKLM_COLS; i++) {
            v[i] += a[i] * BKLM_WATER_STEP_MS / 1000;
            h[i] += v[i] * BKLM_WATER_STEP_MS / 1000;
            h[i] = CONSTRAIN(h[i], 128 * BKLM_WATER_SC, (BKLM_ROWS * 256 - 128) * BKLM_WATER_SC);
            sum += h[i];
        }
        /* Keep the amount of water constant (rounding and the walls drift it). */
        const int32_t drift = sum / BKLM_COLS - rest;
        for (uint8_t i = 0; i < BKLM_COLS; i++) h[i] -= drift;
    }

    for (uint8_t x = 0; x < BKLM_COLS; x++) {
        const uint32_t foam = MIN((uint32_t)(v[x] < 0 ? -v[x] : v[x]) / (12 * BKLM_WATER_SC), 255); /* fast surface -> white */
        for (uint8_t y = 0; y < BKLM_ROWS; y++) {
            const int32_t depth = h[x] / BKLM_WATER_SC - (y * 256 + 128); /* Q8 cells below the surface, at the cell centre */
            if (depth <= -128) continue;                  /* air */
            const uint32_t fill = depth >= 128 ? 256 : (uint32_t)(depth + 128);
            const uint32_t d    = (uint32_t)MAX(depth, 0) * 255 / ((uint32_t)(rest / BKLM_WATER_SC) + 256); /* 0 at surface .. ~255 at the bottom */
            RGB            c    = {
                .r = (uint8_t)(50 - MIN(d, 255) * 50 / 255),
                .g = (uint8_t)(170 - MIN(d, 255) * 150 / 255),
                .b = (uint8_t)(235 - MIN(d, 255) * 175 / 255),
            };
            if (depth < 256) { /* the top cell gets the foam */
                c.r = (uint8_t)(c.r + (255 - c.r) * foam / 255);
                c.g = (uint8_t)(c.g + (255 - c.g) * foam / 255);
                c.b = (uint8_t)(c.b + (255 - c.b) * foam / 255);
            }
            const uint32_t level         = fill * m->fade / 256;
            *bklm_px(pixels, x, y) = (RGB){.r = (uint8_t)(c.r * level / 255), .g = (uint8_t)(c.g * level / 255), .b = (uint8_t)(c.b * level / 255)};
        }
    }
    return true;
}
