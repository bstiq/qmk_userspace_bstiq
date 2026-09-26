// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_mods.h"

#define BKLM_MOD_W 5
#define BKLM_MOD_H 7

enum {
    BKLM_MOD_GUI = 0,
    BKLM_MOD_ALT,
    BKLM_MOD_CTRL,
    BKLM_MOD_SHIFT,
    BKLM_MOD_COUNT
};

/*
 * 5×7 tiles packed beside the 4×6 layer digit so the number stays fully
 * visible. Origin x=5 leaves a one-column gutter after the digit.
 *
 * A 5×7 tile is one row taller than the digit, so the first icon hangs
 * one row below it. Two tiles fit in that column; a third wraps under
 * the digit. Four 5×7 tiles plus the digit cannot pack into 12×16
 * without overlap, so a fourth active mod is not drawn.
 *
 * Glyphs: bit 4 is the left pixel, row 0 is the top (same as the digits).
 * Colors match led_matrix_icon_palette (white / cyan / orange / yellow).
 */
static const struct {
    uint8_t x;
    uint8_t y;
} bklm_mod_slot[] = {
    {5, 0},
    {5, 7},
    {0, 8},
};

static const struct {
    uint8_t mask;
    uint8_t glyph[BKLM_MOD_H];
    RGB     color;
} bklm_mod[BKLM_MOD_COUNT] = {
    [BKLM_MOD_GUI] = {
        .mask  = MOD_MASK_GUI, /* KC_LGUI | KC_RGUI */
        .color = {244, 247, 255},
        .glyph = {
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x00, /* ..... */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
            0x1B, /* ##.## */
        },
    },
    [BKLM_MOD_ALT] = {
        .mask  = MOD_MASK_ALT, /* KC_LALT | KC_RALT */
        .color = {62, 207, 255},
        .glyph = {
            0x1D, /* ###.# */
            0x1D, /* ###.# */
            0x02, /* ...#. */
            0x04, /* ..#.. */
            0x08, /* .#... */
            0x1F, /* ##### */
            0x1F, /* ##### */
        },
    },
    [BKLM_MOD_CTRL] = {
        .mask  = MOD_MASK_CTRL, /* KC_LCTL | KC_RCTL */
        .color = {255, 138, 61},
        .glyph = {
            0x0E, /* .###. */
            0x11, /* #...# */
            0x10, /* #.... */
            0x10, /* #.... */
            0x10, /* #.... */
            0x11, /* #...# */
            0x0E, /* .###. */
        },
    },
    [BKLM_MOD_SHIFT] = {
        .mask  = MOD_MASK_SHIFT, /* KC_LSFT | KC_RSFT */
        .color = {255, 229, 102},
        .glyph = {
            0x04, /* ..#.. */
            0x0E, /* .###. */
            0x1F, /* ##### */
            0x04, /* ..#.. */
            0x04, /* ..#.. */
            0x0E, /* .###. */
            0x1F, /* ##### */
        },
    },
};

/* Bit (width-1) is the left pixel. origin_y is visual top; y=0 is the bottom LED. */
static void bklm_mods_draw_glyph(RGB *pixels, const uint8_t *glyph, uint8_t origin_x, uint8_t origin_y, RGB color) {
    for (uint8_t row = 0; row < BKLM_MOD_H; row++) {
        uint8_t bits = glyph[row];
        for (uint8_t col = 0; col < BKLM_MOD_W; col++) {
            if ((bits & (1u << (BKLM_MOD_W - 1 - col))) == 0) {
                continue;
            }
            uint8_t x   = origin_x + col;
            uint8_t y   = (BKLM_ROWS - 1) - (origin_y + row);
            RGB    *dst = &pixels[(uint16_t)y * BKLM_COLS + x];
            dst->r      = color.r;
            dst->g      = color.g;
            dst->b      = color.b;
        }
    }
}

/* Active GUI / Alt / Ctrl / Shift tiles beside the layer digit.
 * Left and right keys of the same modifier share one icon. No-op when
 * pixels is NULL or no matching modifier is held. */
void bklm_draw_active_modifier_icons(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    const uint8_t mods = get_mods();
    uint8_t       slot = 0;

    for (uint8_t i = 0; i < BKLM_MOD_COUNT; i++) {
        if ((mods & bklm_mod[i].mask) == 0) {
            continue;
        }
        if (slot >= ARRAY_SIZE(bklm_mod_slot)) {
            break;
        }
        bklm_mods_draw_glyph(pixels, bklm_mod[i].glyph, bklm_mod_slot[slot].x, bklm_mod_slot[slot].y, bklm_mod[i].color);
        slot++;
    }
}
