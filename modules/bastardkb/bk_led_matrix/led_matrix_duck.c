// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_duck.h"

#define BKLM_DUCK_W 11
#define BKLM_DUCK_H 13

/* Water never moves. The hull is painted over it, so the duck reads as sitting
 * in the water rather than on top of it.
 *
 * The surface has to reach the row where the body flares out. Any shallower
 * and the hull's curve pulls away from a flat waterline, leaving a wedge of
 * dark cells between the two. */
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
static const RGB bklm_duck_body    = {255, 229, 102}; /* #ffe566 */
static const RGB bklm_duck_beak    = {255, 138, 61};  /* #ff8a3d */
static const RGB bklm_duck_water   = {62, 207, 255};  /* #3ecfff */

/*
 * Char art instead of the PIX_ tables in led_matrix_data.h: those spell one
 * token per cell, which is unreadable at 11x13, and including that header
 * would copy every pointer icon into flash a second time. Here the bitmap is
 * its own comment, so there is nothing to keep in sync.
 *
 *   '.' transparent - water or the dark well shows through
 *   '#' outline     - an edge over water, invisible over the dark well, which
 *                     is what you want in both cases
 *   'O' body
 *   '*' beak        - butted against the body, because the black cell the
 *                     reference picture puts between them would leave the beak
 *                     floating where there is no water behind it
 *
 * Row 0 is the top of the picture.
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

/* Duck floating on a strip of water, bobbing one row every BKLM_DUCK_BOB_MS.
 * Layer 0 only, and only while no modifier is held: the layer digit and the
 * modifier tiles share this well, and the scene is just clutter behind them.
 * No-op when pixels is NULL. */
void bklm_draw_bobbing_duck(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }
    if (get_highest_layer(layer_state) != 0 || get_mods() != 0) {
        return;
    }

    for (uint8_t row = BKLM_ROWS - BKLM_DUCK_WATER_ROWS; row < BKLM_ROWS; row++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            *bklm_duck_pixel_at(pixels, x, row) = bklm_duck_water;
        }
    }

    /* Two positions straight off the millisecond clock, so there is no frame
     * counter to carry. The phase jumps once per timer wrap (~49 days). */
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
