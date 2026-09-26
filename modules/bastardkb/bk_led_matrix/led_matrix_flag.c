// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_flag.h"

#define BKLM_FLAG_W 12
#define BKLM_FLAG_H 14

/* The emblem is two rows shorter than the panel: one blank row above the
 * sickle tip and one below the grips, so the red field frames it evenly. */
#define BKLM_FLAG_TOP_ROW 1

_Static_assert(BKLM_FLAG_W == BKLM_COLS, "flag field has to span the panel, so the sprite is full width");
_Static_assert(BKLM_FLAG_TOP_ROW + BKLM_FLAG_H <= BKLM_ROWS, "flag sprite hangs off the bottom of the panel");

/* The field is painted over every cell, so the emblem is never seen against
 * the dark well and one ink color is enough - no outline to lift it off the
 * background, which at 12x16 would eat the thin parts of the sickle. */
static const RGB bklm_flag_field  = {204, 0, 0};    /* #cc0000 */
static const RGB bklm_flag_emblem = {255, 209, 26}; /* #ffd11a */

/*
 * Char art for the same reasons as the duck: one token per cell is unreadable
 * at this size, and here the bitmap is its own comment.
 *
 *   '.' field  - red
 *   '#' emblem - gold
 *
 * Twelve columns will not hold the reference picture's proportions, so the
 * emblem is redrawn rather than scaled. The panel is portrait and the original
 * is 2:1, which means both handles run much steeper here; where they cross,
 * they merge into a single block for two rows, because two 2-wide strokes
 * cannot pass through each other in three columns. The hammer head is a
 * beveled blob instead of a tilted parallelogram: a tilt four columns wide
 * loses the head's silhouette completely.
 *
 * Row 0 is the top of the picture.
 */
static const char bklm_flag_sprite[BKLM_FLAG_H][BKLM_FLAG_W + 1] = {
    ".....####...", /* sickle tip, blade running right */
    ".###...###..", /* hammer head sits inside the blade's curve */
    ".####....##.",
    ".####.....##",
    "..###.....##",
    "....##....##", /* hammer handle leaves the head */
    ".....##...##",
    ".....##..###", /* blade base, where the sickle handle starts */
    "......####..",
    "......###...", /* the two handles cross */
    "......###...",
    "....##.##...",
    "...##...##..",
    "..##....##..", /* sickle grip on the left, hammer grip on the right */
};

/* Row 0 is the top of the picture; framebuffer y = 0 is the bottom LED. */
static RGB *bklm_flag_pixel_at(RGB *pixels, uint8_t x, uint8_t row) {
    const uint8_t y = (BKLM_ROWS - 1) - row;
    return &pixels[(uint16_t)y * BKLM_COLS + x];
}

/* Gold hammer and sickle on a full red field. Nothing animates: the field
 * lights all 192 cells, so a moving emblem would swing the strip's current
 * instead of just its picture.
 * The scene is the resting state, so it paints unconditionally: the composer
 * calls it only once every indicator has declined the frame.
 * No-op when pixels is NULL. */
void bklm_draw_hammer_and_sickle(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    for (uint8_t row = 0; row < BKLM_ROWS; row++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            *bklm_flag_pixel_at(pixels, x, row) = bklm_flag_field;
        }
    }

    for (uint8_t row = 0; row < BKLM_FLAG_H; row++) {
        for (uint8_t col = 0; col < BKLM_FLAG_W; col++) {
            if (bklm_flag_sprite[row][col] != '#') {
                continue; /* field */
            }
            *bklm_flag_pixel_at(pixels, col, BKLM_FLAG_TOP_ROW + row) = bklm_flag_emblem;
        }
    }
}
