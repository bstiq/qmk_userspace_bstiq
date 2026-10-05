// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_layers.h"
#include "led_matrix_motion.h"

/*
 * Wave and chevron: brightness bands along the direction of motion; the
 * chevron bends them back by the distance across it, into arrows.
 *
 * Cell positions are in half-cells from the panel centre (12x16 has no centre
 * cell), the direction is a unit vector * 127, the phase 1/256 of a wavelength.
 */

/* Raised cosine, one wavelength in 32 steps: 255 on a crest, 0 in a trough. */
static const uint8_t bklm_band_shape[32] = {
    255, 253, 245, 234, 218, 198, 176, 152, 128, 103, 79, 57, 37, 21, 10, 2,
    0,   2,   10,  21,  37,  57,  79,  103, 127, 152, 176, 198, 218, 234, 245, 253,
};

bool bklm_motion_bands(RGB *pixels, const bklm_motion_t *m, bool chevron) {
    static uint16_t phase;

    /* A floor so slow moves still show, full at FULL_SPEED, times the fade. */
    const uint32_t amp = (96 + 159 * (uint32_t)m->norm / 255) * m->fade / 255;

    /* 1.5 to 5.5 wavelengths per second, scaled by speed. */
    const uint32_t milliwaves_per_s = 1500 + 4000 * (uint32_t)m->norm / 255;
    phase += (uint16_t)(milliwaves_per_s * m->dt * 256 / 1000000);

#if !LED_MATRIX_MODULE_WAVE_RAINBOW
    const RGB color = bklm_get_active_layer_color();
#endif
    /* One wavelength in half-cells, times 127 for the unit vector. */
    const int32_t wave_units = 2 * LED_MATRIX_MODULE_WAVE_LENGTH * 127;

    for (uint8_t y = 0; y < BKLM_ROWS; y++) {
        for (uint8_t x = 0; x < BKLM_COLS; x++) {
            const int32_t cx    = 2 * x - (BKLM_COLS - 1);
            const int32_t cy    = 2 * y - (BKLM_ROWS - 1);
            int32_t       along = cx * m->dir_x + cy * m->dir_y;
            if (chevron) {
                const int32_t across = cx * -m->dir_y + cy * m->dir_x;
                along -= (across < 0 ? -across : across) / 2; /* apex ahead, arms trailing at 1:2 */
            }
            /* Crests sit where along * 256 / wave_units == phase, so as the phase
             * grows they move towards +direction: the way the ball rolls. */
            const uint8_t idx   = (uint8_t)((along * 256 / wave_units) - phase);
            uint32_t      level = bklm_band_shape[idx >> 3];
            level               = level * level / 255; /* narrower crests, darker gaps */
            level               = level * amp / 255;

            RGB *dst = bklm_px(pixels, x, y);
#if LED_MATRIX_MODULE_WAVE_RAINBOW
            /* Hue runs along the motion, one turn per RAINBOW_SPAN wavelengths,
             * offset by the same phase so each crest keeps its colour. */
            const uint8_t hue = (uint8_t)(along * 256 / (wave_units * LED_MATRIX_MODULE_WAVE_RAINBOW_SPAN) - phase / LED_MATRIX_MODULE_WAVE_RAINBOW_SPAN);
            *dst              = hsv_to_rgb((HSV){.h = hue, .s = 255, .v = (uint8_t)level});
#else
            dst->r = (uint8_t)(color.r * level / 255);
            dst->g = (uint8_t)(color.g * level / 255);
            dst->b = (uint8_t)(color.b * level / 255);
#endif
        }
    }
    return true;
}
