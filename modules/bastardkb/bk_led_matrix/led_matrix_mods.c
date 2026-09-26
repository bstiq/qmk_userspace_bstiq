// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_layers.h"
#include "led_matrix_mods.h"

#define BKLM_MOD_W 5
#define BKLM_MOD_H 8

enum {
    BKLM_MOD_CAPS = 0,
    BKLM_MOD_SHIFT,
    BKLM_MOD_ALT,
    BKLM_MOD_GUI,
    BKLM_MOD_COUNT
};

/*
 * One row per corner tile. x/y are visual top-left.
 *
 * 5×8 tiles, 2-column gutter, rows stacked flush: they fill the 12×16
 * well. Caps shares the top-left with the 4×6 layer digit; housekeeping
 * draws the digit last so the number stays on top.
 *
 * Glyphs: bit 4 is the left pixel, row 0 is the top (same as the digits).
 * Colors match led_matrix_icon_palette (orange / yellow / cyan / white).
 */
static const struct {
    uint8_t x;
    uint8_t y;
    uint8_t glyph[BKLM_MOD_H];
    RGB     color;
} bklm_mod[BKLM_MOD_COUNT] = {
    [BKLM_MOD_CAPS] = {
        .x     = 0,
        .y     = 0,
        .color = {255, 138, 61},
        .glyph = {
            0x0E, /* .###. */
            0x0A, /* .#.#. */
            0x0A, /* .#.#. */
            0x1F, /* ##### */
            0x11, /* #...# */
            0x15, /* #.#.# */
            0x11, /* #...# */
            0x1F, /* ##### */
        },
    },
    [BKLM_MOD_SHIFT] = {
        .x     = 7,
        .y     = 0,
        .color = {255, 229, 102},
        .glyph = {
            0x04, /* ..#.. */
            0x0E, /* .###. */
            0x1F, /* ##### */
            0x04, /* ..#.. */
            0x04, /* ..#.. */
            0x04, /* ..#.. */
            0x0E, /* .###. */
            0x1F, /* ##### */
        },
    },
    [BKLM_MOD_ALT] = {
        .x     = 0,
        .y     = 8,
        .color = {62, 207, 255},
        .glyph = {
            0x1D, /* ###.# */
            0x1D, /* ###.# */
            0x02, /* ...#. */
            0x04, /* ..#.. */
            0x08, /* .#... */
            0x1F, /* ##### */
            0x1F, /* ##### */
            0x00, /* ..... */
        },
    },
    [BKLM_MOD_GUI] = {
        .x     = 7,
        .y     = 8,
        .color = {244, 247, 255},
        .glyph = {
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x00, /* ..... */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x00, /* ..... */
        },
    },
};

/* Corner tiles for Caps / Shift / Alt / GUI while those keys are down.
 * Oneshots and weak mods count so sticky and tap-hold look like a held
 * key. A non-base layer tints every tile with that layer's color so the
 * well reads as one state. No-op when pixels is NULL. */
void bklm_draw_active_modifier_icons(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    const uint8_t mods = mod_config(get_mods() | get_oneshot_mods() | get_oneshot_locked_mods() | get_weak_mods());
    const bool    on[BKLM_MOD_COUNT] = {
        [BKLM_MOD_CAPS]  = host_keyboard_led_state().caps_lock,
        [BKLM_MOD_SHIFT] = (mods & MOD_MASK_SHIFT) != 0,
        [BKLM_MOD_ALT]   = (mods & MOD_MASK_ALT) != 0,
        [BKLM_MOD_GUI]   = (mods & MOD_MASK_GUI) != 0,
    };

    RGB        layer_color;
    const bool layer_tint = bklm_get_active_layer_and_color(&layer_color) != 0;

    for (uint8_t i = 0; i < BKLM_MOD_COUNT; i++) {
        if (on[i]) {
            const RGB color = layer_tint ? layer_color : bklm_mod[i].color;
            bklm_draw_bitmap_glyph(pixels, bklm_mod[i].glyph, BKLM_MOD_W, BKLM_MOD_H, bklm_mod[i].x, bklm_mod[i].y, color);
        }
    }
}
