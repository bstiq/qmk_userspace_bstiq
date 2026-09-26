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

/* Top-left 4×6 well. Glyph row 0 is visual top; y = 0 is the bottom LED row. */
static void bklm_layers_blit(RGB *pixels, uint8_t layer, RGB color, uint8_t x_origin) {
    const uint8_t *glyph = bklm_layer_digit[layer - 1];

    for (uint8_t row = 0; row < BKLM_LAYER_DIGIT_H; row++) {
        uint8_t bits = glyph[row];
        for (uint8_t col = 0; col < BKLM_LAYER_DIGIT_W; col++) {
            if ((bits & (1u << (BKLM_LAYER_DIGIT_W - 1 - col))) == 0) {
                continue;
            }
            uint8_t x   = x_origin + col;
            uint8_t y   = (BKLM_ROWS - 1) - row;
            RGB    *dst = &pixels[(uint16_t)y * BKLM_COLS + x];
            dst->r      = color.r;
            dst->g      = color.g;
            dst->b      = color.b;
        }
    }
}

/* Layer digit in the top-left 4×6 well when the active layer is 1–9.
 * No-op when pixels is NULL or the highest layer is 0. */
void bklm_layers_paint(RGB *pixels) {
    if (pixels == NULL) {
        return;
    }

    uint8_t layer = get_highest_layer(layer_state);
    if (layer == 0) {
        return;
    }
    if (layer > 9) {
        layer = 9;
    }

    RGB color = {255, 255, 255};
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
    argos_rgb_get_layer_color(layer, &color);
    if (color.r == 0 && color.g == 0 && color.b == 0) {
        color.r = 255;
        color.g = 255;
        color.b = 255;
    }
#endif

    uint8_t x_origin = 0;
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
    if (layer >= 1) {
        x_origin = 1;
    }
#endif

    bklm_layers_blit(pixels, layer, color, x_origin);
}
