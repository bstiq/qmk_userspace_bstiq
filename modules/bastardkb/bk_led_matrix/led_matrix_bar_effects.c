// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_bar_effects.h"

#include <string.h>

#if defined(RGB_MATRIX_ENABLE)
#    include "rgb_matrix.h"
#endif

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#define BKLM_BAR_EFFECT_LEDS 16

/*
 * Side-bar approximations of QMK rgb_matrix effects (see docs.qmk.fm rgb_matrix effects).
 * Only a subset is animated here; others fall back to the current HSV swatch.
 */
#ifdef RGB_MATRIX_ENABLE

extern uint32_t g_rgb_timer;

typedef struct {
    uint8_t     mode;
    const char *name;
} bklm_bar_effect_info_t;

/* Subset documented here; unlisted modes resolve via rgb_matrix_get_mode_name(). */
static const bklm_bar_effect_info_t bklm_bar_effect_table[] = {
    {RGB_MATRIX_GRADIENT_UP_DOWN, "gradient_up_down"},
    {RGB_MATRIX_GRADIENT_LEFT_RIGHT, "gradient_left_right"},
    {RGB_MATRIX_CYCLE_LEFT_RIGHT, "cycle_left_right"},
    {RGB_MATRIX_CYCLE_UP_DOWN, "cycle_up_down"},
};

#endif

#ifdef RGB_MATRIX_ENABLE

/* Full-range RGB; bklm_show applies keyboard brightness once. */
static RGB bklm_bar_hsv_pixel(uint8_t hue, uint8_t sat) {
    if (sat == 0) {
        sat = UINT8_MAX;
    }
    return hsv_to_rgb((HSV){hue, sat, UINT8_MAX});
}

/* Matches rgb_matrix effect_runner_i: scale16by8(g_rgb_timer, qadd8(speed / 4, 1)). */
static uint8_t bklm_bar_runner_time(void) {
    const uint8_t scale = (uint8_t)(rgb_matrix_get_speed() / 4 + 1);
    return (uint8_t)(((uint16_t)g_rgb_timer * scale) >> 8);
}

static uint8_t bklm_bar_index_to_coord(uint8_t bar_index) {
    if (BKLM_BAR_EFFECT_LEDS <= 1) {
        return 0;
    }
    return (uint8_t)((bar_index * 255) / (BKLM_BAR_EFFECT_LEDS - 1));
}

static RGB bklm_bar_solid_fallback(void) {
    return bklm_bar_hsv_pixel(rgb_matrix_get_hue(), rgb_matrix_get_sat());
}

#ifdef RGB_MATRIX_MODE_NAME_ENABLE
static bool bklm_bar_mode_name_has(uint8_t mode, const char *fragment) {
    const char *name = rgb_matrix_get_mode_name(mode);
    if (name == NULL || fragment == NULL) {
        return false;
    }
    return strstr(name, fragment) != NULL;
}
#endif

static bool bklm_bar_mode_is_cycle_left_right(uint8_t mode) {
#ifdef ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
    if (mode == RGB_MATRIX_CYCLE_LEFT_RIGHT) {
        return true;
    }
#endif
#ifdef RGB_MATRIX_MODE_NAME_ENABLE
    if (bklm_bar_mode_name_has(mode, "Cycle Left Right")) {
        return true;
    }
    if (bklm_bar_mode_name_has(mode, "Left Right")) {
        return true;
    }
    if (bklm_bar_mode_name_has(mode, "LEFT_RIGHT")) {
        return true;
    }
#endif
    return false;
}

static RGB bklm_bar_gradient_up_down(uint8_t bar_index, uint32_t now_ms) {
    const uint8_t spread = rgb_matrix_get_speed();
    const uint8_t phase  = (uint8_t)((now_ms >> 6) + spread);
    const uint8_t step   = (bar_index * 255) / (BKLM_BAR_EFFECT_LEDS - 1);
    const uint8_t hue = rgb_matrix_get_hue() + phase + step;
    return bklm_bar_hsv_pixel(hue, rgb_matrix_get_sat());
}

static RGB bklm_bar_gradient_left_right(uint8_t bar_index, uint32_t now_ms) {
    const uint8_t phase = (uint8_t)((now_ms >> 6) + rgb_matrix_get_speed());
    const uint8_t step  = bklm_bar_index_to_coord(bar_index);
    const uint8_t hue   = rgb_matrix_get_hue() + phase + step;
    return bklm_bar_hsv_pixel(hue, rgb_matrix_get_sat());
}

static uint8_t bklm_bar_matrix_led_coord_x(uint8_t bar_index) {
    if (bar_index < RGB_MATRIX_LED_COUNT) {
        return g_led_config.point[bar_index].x;
    }
    return bklm_bar_index_to_coord(bar_index);
}

static uint8_t bklm_bar_matrix_led_coord_y(uint8_t bar_index) {
    if (bar_index < RGB_MATRIX_LED_COUNT) {
        return g_led_config.point[bar_index].y;
    }
    return bklm_bar_index_to_coord(bar_index);
}

/*
 * QMK cycle_left_right: hsv.h = g_led_config.point[i].x - time (hue is replaced).
 */
static RGB bklm_bar_cycle_left_right(uint8_t bar_index, uint32_t now_ms) {
    (void)now_ms;
    const uint8_t hue = bklm_bar_matrix_led_coord_x(bar_index) - bklm_bar_runner_time();
    return bklm_bar_hsv_pixel(hue, rgb_matrix_get_sat());
}

static RGB bklm_bar_cycle_up_down(uint8_t bar_index, uint32_t now_ms) {
    (void)now_ms;
    const uint8_t hue = bklm_bar_matrix_led_coord_y(bar_index) - bklm_bar_runner_time();
    return bklm_bar_hsv_pixel(hue, rgb_matrix_get_sat());
}

#endif /* RGB_MATRIX_ENABLE */

const char *bklm_bar_effect_mode_name(uint8_t effect_mode) {
#ifdef RGB_MATRIX_ENABLE
    for (size_t i = 0; i < ARRAY_SIZE(bklm_bar_effect_table); i++) {
        if (bklm_bar_effect_table[i].mode == effect_mode) {
            return bklm_bar_effect_table[i].name;
        }
    }
    return rgb_matrix_get_mode_name(effect_mode);
#else
    (void)effect_mode;
    return "unknown";
#endif
}

RGB bklm_bar_effect_rgb(uint8_t bar_index, uint8_t effect_mode, uint32_t now_ms) {
#if !defined(RGB_MATRIX_ENABLE)
    (void)bar_index;
    (void)effect_mode;
    (void)now_ms;
    return (RGB){0, 0, 0};
#else
    if (!rgb_matrix_is_enabled()) {
        return (RGB){0, 0, 0};
    }

    if (bar_index >= BKLM_BAR_EFFECT_LEDS) {
        return (RGB){0, 0, 0};
    }

    switch (effect_mode) {
#ifdef ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
        case RGB_MATRIX_GRADIENT_UP_DOWN:
            return bklm_bar_gradient_up_down(bar_index, now_ms);
#endif
#ifdef ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
        case RGB_MATRIX_GRADIENT_LEFT_RIGHT:
            return bklm_bar_gradient_left_right(bar_index, now_ms);
#endif
#ifdef ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
        case RGB_MATRIX_CYCLE_LEFT_RIGHT:
            return bklm_bar_cycle_left_right(bar_index, now_ms);
#endif
#ifdef ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
        case RGB_MATRIX_CYCLE_UP_DOWN:
            return bklm_bar_cycle_up_down(bar_index, now_ms);
#endif
        default:
            if (bklm_bar_mode_is_cycle_left_right(effect_mode)) {
                return bklm_bar_cycle_left_right(bar_index, now_ms);
            }
            return bklm_bar_solid_fallback();
    }
#endif
}
