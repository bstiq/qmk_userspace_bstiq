// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include <string.h>

/* Pulls the bitmap tables into this file only. The header keeps types/palette
 * visible to the display side without duplicating the pixel data. */
#define LED_MATRIX_ICONS_DEFINE
#include "led_matrix_pointer.h"

#if __has_include("bk_pointing_modes.h")
#    include "bk_pointing_modes.h"
#endif

/* Built-in logos by mode id. Missing entries (Normal, custom, gaps) are NULL. */
#if __has_include("bk_pointing_modes.h")
static const led_matrix_icon_t *const led_matrix_icon_by_mode[] = {
    [MODE_SNIPING]    = &led_matrix_icon_sniping,
    [MODE_DRAGSCROLL] = &led_matrix_icon_dragscroll,
    [MODE_CURSOR]     = &led_matrix_icon_cursor,
    [MODE_BRIGHTNESS] = &led_matrix_icon_brightness,
    [MODE_ZOOM]       = &led_matrix_icon_zoom,
    [MODE_VOLUME]     = &led_matrix_icon_volume,
    [MODE_TAB_SWITCH] = &led_matrix_icon_tab_switch,
    [MODE_HISTORY]    = &led_matrix_icon_history,
};

/* One buffer for C+digit. Rebuilt when a custom mode is active. */
static led_matrix_icon_t led_matrix_icon_custom;

/* Letter C on rows 0–5, blank row 6, digit on 7–11. `slot` is 0–4. */
static const led_matrix_icon_t *led_matrix_icon_compose_custom(uint8_t slot) {
    if (slot > 4) {
        return NULL;
    }

    memset(&led_matrix_icon_custom, 0, sizeof(led_matrix_icon_custom));

    memcpy(led_matrix_icon_custom.pixel, led_matrix_icon_custom_letter, sizeof(led_matrix_icon_custom_letter));
    memcpy(&led_matrix_icon_custom.pixel[7], led_matrix_icon_custom_digit[slot], sizeof(led_matrix_icon_custom_digit[slot]));

    return &led_matrix_icon_custom;
}
#endif

/* Map the pointing module's active mode to a logo. Normal and unknown modes
 * return NULL so the display keeps the WPM gauge / layer digit / mods. */
const led_matrix_icon_t *led_matrix_pointer_active_icon(void) {
#if __has_include("bk_pointing_modes.h")
    uint8_t mode = bkpd_mode_get_active_id();

    if (mode >= MODE_CUSTOM1 && mode <= MODE_CUSTOM5) {
        return led_matrix_icon_compose_custom((uint8_t)(mode - MODE_CUSTOM1));
    }
    if (mode < ARRAY_SIZE(led_matrix_icon_by_mode)) {
        return led_matrix_icon_by_mode[mode];
    }
#endif
    return NULL;
}
