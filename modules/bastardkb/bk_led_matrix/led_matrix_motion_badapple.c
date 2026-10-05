// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"
#include "led_matrix_badapple_data.h"

/*
 * Bad Apple!! (not reactive): the shadow-art PV at 15 fps, 12x16, four grey
 * levels (led_matrix_badapple_data.h, from tools/led-matrix-sim/badapple.py).
 * Frames are run-length encoded, so they are decoded in order; a late frame
 * skips ahead by decoding the ones it missed.
 */

static const uint8_t bklm_badapple_grey[4] = {0, 70, 160, 235};

bool bklm_motion_badapple(RGB *pixels, const bklm_motion_t *m) {
    static uint32_t played;      /* ms into the video */
    static uint32_t pos;         /* byte offset of the next frame to decode */
    static uint16_t frame;       /* that frame's number */
    static uint8_t  cells[BKLM_ROWS * BKLM_COLS];

    if (m->fresh) {
        played = 0, pos = 0, frame = 0;
        memset(cells, 0, sizeof(cells));
    }
    played += m->dt;
    uint32_t want = played * BKLM_BADAPPLE_FPS / 1000;
    if (want >= BKLM_BADAPPLE_FRAMES) { /* the end: loop */
        played = 0, pos = 0, frame = 0, want = 0;
    }

    /* Decode up to and including frame `want`. */
    while (frame <= want) {
        uint16_t i = 0;
        while (i < BKLM_ROWS * BKLM_COLS) {
            const uint8_t b = bklm_badapple_rle[pos++];
            for (uint8_t n = (b & 63) + 1; n && i < BKLM_ROWS * BKLM_COLS; n--) cells[i++] = b >> 6;
        }
        frame++;
    }

    for (uint8_t row = 0; row < BKLM_ROWS; row++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            const uint8_t v = bklm_badapple_grey[cells[row * BKLM_COLS + x]];
            *bklm_px(pixels, x, (uint8_t)(BKLM_ROWS - 1 - row)) = (RGB){.r = v, .g = v, .b = v};
        }
    }
    return true;
}
