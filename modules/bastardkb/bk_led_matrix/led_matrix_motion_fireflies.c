// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Fireflies: a pulsing swarm chasing a point the ball moves like a cursor.
 * When the ball stops the point drifts back to the centre and the flies wander
 * for a while before fading out.
 */

#define BKLM_FLIES 11

typedef struct {
    int32_t  x, y;   /* Q8 cells */
    int32_t  vx, vy; /* Q8 cells per second */
    uint16_t phase;  /* glow pulse, 1/65536 of a cycle */
    uint16_t rate;   /* pulse speed */
    uint8_t  hue;
} bklm_fly_t;

static int32_t bklm_fly_rand(int32_t span) {
    return (int32_t)(bklm_rand() % (uint32_t)(2 * span + 1)) - span;
}

bool bklm_motion_fireflies(RGB *pixels, const bklm_motion_t *m) {
    static bklm_fly_t flies[BKLM_FLIES];
    static int32_t    tx, ty; /* the point they chase, Q8 cells */

    if (m->fresh) {
        tx = BKLM_COLS * 128;
        ty = BKLM_ROWS * 128;
        for (uint8_t i = 0; i < BKLM_FLIES; i++) {
            flies[i] = (bklm_fly_t){
                .x     = tx + bklm_fly_rand(4 * 256),
                .y     = ty + bklm_fly_rand(6 * 256),
                .phase = (uint16_t)bklm_rand(),
                .rate  = (uint16_t)(60 + bklm_rand() % 60),
                .hue   = (uint8_t)(42 + bklm_rand() % 30), /* yellow to lime */
            };
        }
    }
    const bool moving = m->still_ms < 60;
    if (moving) {
        bklm_motion_linger(LED_MATRIX_MODULE_FIREFLIES_LINGER_MS);
    }

    tx += bklm_motion_step(m->vx, m->dt, LED_MATRIX_MODULE_FIREFLIES_COUNTS_PER_CELL);
    ty += bklm_motion_step(m->vy, m->dt, LED_MATRIX_MODULE_FIREFLIES_COUNTS_PER_CELL);
    if (!moving) {
        tx += (BKLM_COLS * 128 - tx) * (int32_t)m->dt / 2000;
        ty += (BKLM_ROWS * 128 - ty) * (int32_t)m->dt / 2000;
    }
    tx = CONSTRAIN(tx, 256, (BKLM_COLS - 1) * 256);
    ty = CONSTRAIN(ty, 256, (BKLM_ROWS - 1) * 256);

    const int32_t dt = (int32_t)m->dt;
    for (uint8_t i = 0; i < BKLM_FLIES; i++) {
        bklm_fly_t *f = &flies[i];
        /* Stronger pull while the ball moves, so the swarm keeps up. */
        const int32_t k  = moving ? 4 : 1;
        int32_t       ax = (tx - f->x) * k + bklm_fly_rand(24 * 256);
        int32_t       ay = (ty - f->y) * k + bklm_fly_rand(24 * 256);
        /* Keep a little apart, so the swarm stays a swarm and not one blob. */
        for (uint8_t j = 0; j < BKLM_FLIES; j++) {
            const int32_t ddx = f->x - flies[j].x, ddy = f->y - flies[j].y;
            if (j != i && ddx > -384 && ddx < 384 && ddy > -384 && ddy < 384) {
                ax += ddx * 24;
                ay += ddy * 24;
            }
        }
        f->vx += ax * dt / 1000;
        f->vy += ay * dt / 1000;
        f->vx -= f->vx * 3 * dt / 1000;
        f->vy -= f->vy * 3 * dt / 1000;
        f->vx = CONSTRAIN(f->vx, -14 * 256, 14 * 256);
        f->vy = CONSTRAIN(f->vy, -14 * 256, 14 * 256);
        f->x  = CONSTRAIN(f->x + f->vx * dt / 1000, 0, BKLM_COLS * 256 - 1);
        f->y  = CONSTRAIN(f->y + f->vy * dt / 1000, 0, BKLM_ROWS * 256 - 1);

        f->phase += (uint16_t)(f->rate * dt);
        const uint32_t tri   = f->phase < 32768 ? f->phase >> 7 : (65535 - f->phase) >> 7; /* 0..255 */
        const uint32_t glow  = 110 + 145 * tri * tri / 65025;
        const uint8_t  level = (uint8_t)(glow * m->fade / 255);
        bklm_splat(pixels, f->x, f->y, hsv_to_rgb((HSV){.h = f->hue, .s = 210, .v = 255}), level, false);
    }
    return true;
}
