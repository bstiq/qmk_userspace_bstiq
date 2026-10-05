// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Rolling ball: a shaded disc with a tiled surface that rolls with the
 * trackball. Each cell is mapped back onto the sphere by stretching its
 * distance towards the rim, then offset by the roll.
 * The radius is 11.5 half-cells, so the disc just fills the 12 columns.
 */

#define BKLM_BALL_R 23 /* radius, in quarter-cells: 11.5 half-cells */

bool bklm_motion_ball(RGB *pixels, const bklm_motion_t *m) {
    static int32_t u, v;   /* roll offset, Q8 cells */
    static uint8_t hue0;   /* base hue, drifts slowly */

    u += bklm_motion_step(m->vx, m->dt, LED_MATRIX_MODULE_BALL_COUNTS_PER_CELL);
    v += bklm_motion_step(m->vy, m->dt, LED_MATRIX_MODULE_BALL_COUNTS_PER_CELL);
    hue0 += (uint8_t)(m->dt / 16);

    const int32_t r2 = BKLM_BALL_R * BKLM_BALL_R; /* quarter-cells squared */

    for (uint8_t y = 0; y < BKLM_ROWS; y++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            /* Quarter-cell offsets from the centre: (2x - 11) half-cells = 2 * that in quarters. */
            const int32_t qx = 2 * (2 * x - (BKLM_COLS - 1)), qy = 2 * (2 * y - (BKLM_ROWS - 1));
            const int32_t d2 = qx * qx + qy * qy;
            if (d2 > r2) continue;
            const int32_t z = (int32_t)bklm_isqrt((uint32_t)(r2 - d2) * 256) / 16; /* quarter-cells */

            /* Surface position, Q8 cells: stretched towards the rim by 2R / (z + R),
             * roughly the arc length on a sphere. qx is in quarter-cells, so *64 for Q8. */
            const int32_t stretch = 2 * BKLM_BALL_R * 256 / (z + BKLM_BALL_R); /* Q8 factor */
            const int32_t sx      = qx * 64 * stretch / 256 - u;
            const int32_t sy      = qy * 64 * stretch / 256 - v;

            /* Tiles of 3x3 cells; each gets its own hue, every other one darker. */
            const int32_t tx = (sx >= 0 ? sx : sx - 3 * 256 + 1) / (3 * 256);
            const int32_t ty = (sy >= 0 ? sy : sy - 3 * 256 + 1) / (3 * 256);
            const uint8_t hue  = (uint8_t)(hue0 + tx * 53 + ty * 97);
            const uint8_t tone = ((tx + ty) & 1) ? 255 : 120;

            /* Lambert-ish: brighter where the surface faces you, from the top left. */
            const int32_t lit   = 70 + 185 * (z * 3 - qx + qy) / (BKLM_BALL_R * 4);
            const uint32_t light = (uint32_t)CONSTRAIN(lit, 40, 255);
            uint32_t       level = light * tone / 255 * m->fade / 255;

            RGB *dst = bklm_px(pixels, x, y);
            *dst     = hsv_to_rgb((HSV){.h = hue, .s = 230, .v = (uint8_t)level});

            const int32_t hx = qx + 9, hy = qy - 9;
            if (hx * hx + hy * hy < 30) {
                bklm_add(dst, (RGB){.r = 255, .g = 255, .b = 255}, (uint8_t)(m->fade * 2 / 3));
            }
        }
    }
    return true;
}
