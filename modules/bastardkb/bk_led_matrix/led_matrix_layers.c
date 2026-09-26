// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_display.h"
#include "led_matrix_layers.h"

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos_rgb.h"
#endif

#define BKLM_LAYER_DIGIT_W 4
#define BKLM_LAYER_DIGIT_H 6

/* Digits 1–9 only. Bit 3 is the left pixel; row 0 is the top of the glyph. */
static const uint8_t bklm_layer_digit[9][BKLM_LAYER_DIGIT_H] = {
    {0x4, 0xC, 0x4, 0x4, 0x4, 0xE}, /* 1 */
    {0xE, 0x1, 0xE, 0x8, 0x8, 0xF}, /* 2 */
    {0xE, 0x1, 0x6, 0x1, 0x1, 0xE}, /* 3 */
    {0x9, 0x9, 0xF, 0x1, 0x1, 0x1}, /* 4 */
    {0xF, 0x8, 0xE, 0x1, 0x1, 0xE}, /* 5 */
    {0x6, 0x8, 0xE, 0x9, 0x9, 0x6}, /* 6 */
    {0xF, 0x1, 0x2, 0x4, 0x4, 0x4}, /* 7 */
    {0x6, 0x9, 0x6, 0x9, 0x9, 0x6}, /* 8 */
    {0x6, 0x9, 0x9, 0x7, 0x1, 0x6}, /* 9 */
};

static const RGB bklm_layer_white = {255, 255, 255};

/* Highest layer 1–9 and the color its glyphs should use, so the digit
 * and the mod tiles agree on tint. Returns 0 on the base layer and
 * leaves color alone. Argos missing or a black underglow swatch falls
 * back to white. */
uint8_t bklm_get_active_layer_and_color(RGB *color) {
    uint8_t layer = get_highest_layer(layer_state);
    if (layer == 0) {
        return 0;
    }
    if (layer > 9) {
        layer = 9;
    }

    *color = bklm_layer_white;
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
    argos_rgb_get_layer_color(layer, color);
    if (color->r == 0 && color->g == 0 && color->b == 0) {
        *color = bklm_layer_white;
    }
#endif
    return layer;
}

/* Layer digit in the top-left 4×6 well when the active layer is 1–9.
 * No-op when pixels is NULL or the highest layer is 0. */
void bklm_layers_paint(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    RGB     color;
    uint8_t layer = bklm_get_active_layer_and_color(&color);
    if (layer == 0) {
        return;
    }

    bklm_draw_bitmap_glyph(pixels, bklm_layer_digit[layer - 1], BKLM_LAYER_DIGIT_W, BKLM_LAYER_DIGIT_H, 0, 0, color);
}
