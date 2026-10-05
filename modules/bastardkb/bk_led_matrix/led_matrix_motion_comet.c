// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Comet: a wrapping dot driven by the ball, leaving a decaying rainbow trail
 * kept in its own framebuffer. A fast frame can move the head several cells,
 * so it is stamped every half cell along the way, or the trail would be dotted.
 */

#define BKLM_COMET_W (BKLM_COLS * 256)
#define BKLM_COMET_H (BKLM_ROWS * 256)

bool bklm_motion_comet(RGB *pixels, const bklm_motion_t *m) {
    static RGB     trail[BKLM_COLS * BKLM_ROWS];
    static int32_t x, y; /* head, Q8 cells */
    static uint8_t hue;

    if (m->fresh) {
        memset(trail, 0, sizeof(trail));
        x = BKLM_COMET_W / 2;
        y = BKLM_COMET_H / 2;
    }

    /* Keep (256 - dt * DECAY) / 256 of the trail this frame. */
    const uint32_t keep = m->dt * LED_MATRIX_MODULE_COMET_DECAY >= 256 ? 0 : 256 - m->dt * LED_MATRIX_MODULE_COMET_DECAY;
    for (uint16_t i = 0; i < BKLM_COLS * BKLM_ROWS; i++) {
        trail[i].r = (uint8_t)(trail[i].r * keep >> 8);
        trail[i].g = (uint8_t)(trail[i].g * keep >> 8);
        trail[i].b = (uint8_t)(trail[i].b * keep >> 8);
    }

    const int32_t dx    = bklm_motion_step(m->vx, m->dt, LED_MATRIX_MODULE_COMET_COUNTS_PER_CELL);
    const int32_t dy    = bklm_motion_step(m->vy, m->dt, LED_MATRIX_MODULE_COMET_COUNTS_PER_CELL);
    const int32_t span  = MAX(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
    const int32_t steps = MIN(span / 128 + 1, 32);
    for (int32_t s = 1; s <= steps; s++) {
        const int32_t sx = x + dx * s / steps, sy = y + dy * s / steps;
        hue += 3; /* colour cycles with distance travelled */
        bklm_splat(trail, sx, sy, hsv_to_rgb((HSV){.h = hue, .s = 255, .v = 255}), 255, true);
    }
    x = (x + dx) % BKLM_COMET_W;
    y = (y + dy) % BKLM_COMET_H;
    if (x < 0) x += BKLM_COMET_W;
    if (y < 0) y += BKLM_COMET_H;

    for (uint16_t i = 0; i < BKLM_COLS * BKLM_ROWS; i++) {
        bklm_add(&pixels[i], trail[i], m->fade);
    }
    bklm_splat(pixels, x, y, (RGB){.r = 255, .g = 255, .b = 255}, (uint8_t)(m->fade * 3 / 4), true);
    return true;
}
