// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_bar.h"
#include "led_matrix_bar_effects.h"
#include "led_matrix_display.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#define BKLM_BAR_LEDS 16

bool bklm_rgb_indicators_should_defer(void) {
    return false;
}

bool bklm_rgb_indicators_caps_lock_override(RGB *out) {
    (void)out;
    return false;
}

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos_rgb.h"

static RGB bklm_bar_argos_rgb(const argos_rgb_t *entry) {
    rgb_t rgb = {0, 0, 0};
    rgb.r     = (entry->r * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    rgb.g     = (entry->g * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    rgb.b     = (entry->b * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    if (rgb.r > RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40) {
        rgb.r = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40;
    }
    if (rgb.g > RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40) {
        rgb.g = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40;
    }
    if (rgb.b > RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40) {
        rgb.b = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 40;
    }
    return (RGB){rgb.r, rgb.g, rgb.b};
}

static RGB bklm_bar_layer_color_scaled(uint8_t layer) {
    RGB color = {255, 255, 255};
    argos_rgb_get_layer_color(layer, &color);
    if (color.r == 0 && color.g == 0 && color.b == 0) {
        color.r = 255;
        color.g = 255;
        color.b = 255;
    }
    argos_rgb_t entry = {color.r, color.g, color.b, false, true, true};
    return bklm_bar_argos_rgb(&entry);
}
#endif

void bklm_draw_left_bar_column(RGB *pixels, uint32_t now_ms) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
    if (pixels == NULL || bklm_rgb_indicators_should_defer()) {
        return;
    }

    RGB  caps_rgb;
    bool caps_override = bklm_rgb_indicators_caps_lock_override(&caps_rgb);

    const uint8_t layer = get_highest_layer(layer_state);
#    ifdef RGB_MATRIX_ENABLE
    const uint8_t effect_mode = rgb_matrix_get_mode();
#    else
    const uint8_t effect_mode = 0;
#    endif

    for (uint8_t i = 0; i < BKLM_BAR_LEDS; i++) {
        RGB color = {0, 0, 0};

        if (caps_override) {
            color = caps_rgb;
        } else if (layer >= 1) {
            color = bklm_bar_layer_color_scaled(layer);
        } else {
            argos_rgb_t entry = {0};
            argos_rgb_get_led_at_position(&entry, 0, i, 0);
            if (entry.custom && entry.on && !entry.passthrough) {
                color = bklm_bar_argos_rgb(&entry);
            } else {
                /* !custom, passthrough, or custom off → follow rgb_matrix (side bar only). */
                color = bklm_bar_effect_rgb(i, effect_mode, now_ms);
            }
        }

        const uint8_t y           = (BKLM_ROWS - 1) - i;
        pixels[(uint16_t)y * BKLM_COLS] = color;
    }
#else
    (void)pixels;
    (void)now_ms;
#endif
}
