// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Warp speed: stars rush out of a vanishing point pushed off centre along the
 * motion. Each star sits at (x, y) in -256..256 and depth z (Q8, 256 = far
 * plane), and is drawn at vanishing point + (x, y) * F / z.
 */

#define BKLM_WARP_STARS 34
#define BKLM_WARP_NEAR 24 /* Q8 depth at which a star has passed you */
#define BKLM_WARP_F 2     /* cells from the vanishing point at the far plane */

typedef struct {
    int16_t x, y, z;
    int32_t sx, sy; /* last screen position, Q8 cells */
    bool    shown;  /* sx, sy are valid */
} bklm_warp_star_t;

static void bklm_warp_respawn(bklm_warp_star_t *s, bool anywhere) {
    s->x     = (int16_t)((int32_t)(bklm_rand() % 513) - 256);
    s->y     = (int16_t)((int32_t)(bklm_rand() % 513) - 256);
    s->z     = anywhere ? (int16_t)(BKLM_WARP_NEAR + bklm_rand() % (256 - BKLM_WARP_NEAR)) : 256;
    s->shown = false;
}

bool bklm_motion_warp(RGB *pixels, const bklm_motion_t *m) {
    static bklm_warp_star_t stars[BKLM_WARP_STARS];
    static bool             placed;

    if (!placed) {
        for (uint8_t i = 0; i < BKLM_WARP_STARS; i++) bklm_warp_respawn(&stars[i], true);
        placed = true;
    }

    /* Vanishing point: centre, pushed 3 cells along the motion. */
    const int32_t vp_x = BKLM_COLS * 128 + m->dir_x * 3 * 256 / 127;
    const int32_t vp_y = BKLM_ROWS * 128 + m->dir_y * 3 * 256 / 127;
    /* Depth per second: a slow cruise up to the whole field in ~0.3 s. */
    const int32_t dz = (int32_t)((120 + 800 * (uint32_t)m->norm / 255) * m->dt / 1000);

    for (uint8_t i = 0; i < BKLM_WARP_STARS; i++) {
        bklm_warp_star_t *s = &stars[i];
        s->z -= (int16_t)MAX(dz, 1);
        if (s->z < BKLM_WARP_NEAR) {
            bklm_warp_respawn(s, false);
        }
        const int32_t sx = vp_x + s->x * BKLM_WARP_F * 256 / s->z;
        const int32_t sy = vp_y + s->y * BKLM_WARP_F * 256 / s->z;
        if (sx < -256 || sy < -256 || sx > BKLM_COLS * 256 + 256 || sy > BKLM_ROWS * 256 + 256) {
            bklm_warp_respawn(s, false);
            continue;
        }

        const uint32_t near  = 256 - (uint32_t)s->z;
        const uint8_t  level = (uint8_t)((70 + 185 * near / 256) * m->fade / 255);
        const RGB      c     = {.r = (uint8_t)(150 + near * 105 / 256), .g = (uint8_t)(170 + near * 85 / 256), .b = 255};

        /* Streak from last frame's position, brightest at the head. */
        if (s->shown) {
            const int32_t ddx = sx - s->sx, ddy = sy - s->sy;
            const int32_t len = MAX(ddx < 0 ? -ddx : ddx, ddy < 0 ? -ddy : ddy);
            const int32_t n   = MIN(len / 128, 12);
            for (int32_t k = 0; k < n; k++) {
                bklm_splat(pixels, s->sx + ddx * k / (n + 1), s->sy + ddy * k / (n + 1), c, (uint8_t)(level * (k + 2) / (n + 3)), false);
            }
        }
        bklm_splat(pixels, sx, sy, c, level, false);
        s->sx    = sx;
        s->sy    = sy;
        s->shown = true;
    }
    return true;
}
