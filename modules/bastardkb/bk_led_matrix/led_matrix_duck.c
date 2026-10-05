// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_duck.h"

#define BKLM_DUCK_W 11
#define BKLM_DUCK_H 12

/* Static water, painted under the hull. It must reach the row where the body
 * flares out, or a wedge of dark cells shows between hull and waterline. */
#define BKLM_DUCK_WATER_ROWS 3

/* Top row of the sprite when the duck rides low. Bobbing lifts it by one,
 * which uncovers another row of water under the hull. */
#define BKLM_DUCK_TOP_ROW 2

/* An 11-wide sprite in a 12-wide panel cannot be centered. The spare column
 * goes on the left, behind the beak, rather than behind the tail. */
#define BKLM_DUCK_LEFT_COL 1

/* How long the duck holds each of its two positions. */
#define BKLM_DUCK_BOB_MS 2000

_Static_assert(BKLM_DUCK_LEFT_COL + BKLM_DUCK_W <= BKLM_COLS, "duck sprite runs off the right of the panel");
_Static_assert(BKLM_DUCK_TOP_ROW + BKLM_DUCK_H <= BKLM_ROWS, "duck sprite hangs off the bottom of the panel");

/* Same values as led_matrix_icon_palette, kept local so this file does not
 * drag the whole pointer icon table into flash. */
static const RGB bklm_duck_outline = {0, 0, 0};
static const RGB bklm_duck_body    = {156, 131, 4}; /* #9c8304 */
static const RGB bklm_duck_beak    = {255, 138, 61};  /* #ff8a3d */
static const RGB bklm_duck_water   = {62, 207, 255};  /* #3ecfff */

/*
 * Char art instead of the PIX_ tables in led_matrix_data.h, which are unreadable
 * at 11x12 and would copy every pointer icon into flash again. Row 0 is the top.
 *
 *   '.' transparent - water or the dark well shows through
 *   '#' outline     - an edge over water, invisible over the dark well
 *   'O' body
 *   '*' beak        - butted against the body so it never floats over the dark well
 */
static const char bklm_duck_sprite[BKLM_DUCK_H][BKLM_DUCK_W + 1] = {
    "....###....",
    "...#OOO#...",
    "..#OOOOO#..",
    ".#OO#OOO#..",
    "**OOOOOO#..",
    ".#OOOOOO#..",
    "..#OOOO#...",
    "...#OO#....",
    "..#OOOO####",
    ".#OOOOOOOO#",
    "#OOOOOOOOO#",
    ".OOOOOOOO..",
};

/* Row 0 is the top of the picture; framebuffer y = 0 is the bottom LED. */
static RGB *bklm_duck_pixel_at(RGB *pixels, uint8_t x, uint8_t row) {
    const uint8_t y = (BKLM_ROWS - 1) - row;
    return &pixels[(uint16_t)y * BKLM_COLS + x];
}

/* The resting state, so it always paints: the composer calls it only once
 * every indicator has declined the frame. No-op when pixels is NULL. */
void bklm_draw_bobbing_duck(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    for (uint8_t row = BKLM_ROWS - BKLM_DUCK_WATER_ROWS; row < BKLM_ROWS; row++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            *bklm_duck_pixel_at(pixels, x, row) = bklm_duck_water;
        }
    }

    /* Phase straight off the ms clock; it jumps once per timer wrap (~49 days). */
    const uint8_t top = BKLM_DUCK_TOP_ROW - (uint8_t)((timer_read32() / BKLM_DUCK_BOB_MS) & 1);

    for (uint8_t row = 0; row < BKLM_DUCK_H; row++) {
        for (uint8_t col = 0; col < BKLM_DUCK_W; col++) {
            const RGB *color;
            switch (bklm_duck_sprite[row][col]) {
                case '#':
                    color = &bklm_duck_outline;
                    break;
                case 'O':
                    color = &bklm_duck_body;
                    break;
                case '*':
                    color = &bklm_duck_beak;
                    break;
                default:
                    continue; /* transparent: keep the water or the dark well */
            }
            *bklm_duck_pixel_at(pixels, BKLM_DUCK_LEFT_COL + col, top + row) = *color;
        }
    }
}
