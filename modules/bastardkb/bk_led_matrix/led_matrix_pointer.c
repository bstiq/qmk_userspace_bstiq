// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include <string.h>

/* Pulls the bitmap tables into this file only. The header keeps types/palette
 * visible to the display side without duplicating the pixel data. */
#define LED_MATRIX_ICONS_DEFINE
#include "led_matrix_pointer.h"
#include "bk_led_matrix.h"

#include "bk_pointing_modes.h"

/* Built-in logos by mode id. Missing entries (Normal, custom, gaps) are NULL. */
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

#define LED_MATRIX_POINTER_LINGER_MS 200

typedef void (*led_matrix_overlay_fn)(uint8_t dir);

/* Missing entries fall through to the default band. Add a slot to override. */
static const led_matrix_overlay_fn led_matrix_overlay_by_mode[MODE_LAST];

static uint32_t linger_started;
static uint8_t  linger_dir;
static bool     linger_live;

/* Dominant axis. Tie keeps the live axis if one is showing, otherwise X. */
static uint8_t led_matrix_pointer_dir_from_delta(int16_t x, int16_t y) {
    if (x == 0 && y == 0) {
        return LED_MATRIX_DIR_NONE;
    }

    uint16_t ax = (uint16_t)((x < 0) ? -(int32_t)x : x);
    uint16_t ay = (uint16_t)((y < 0) ? -(int32_t)y : y);
    bool     use_x;

    if (ax > ay) {
        use_x = true;
    } else if (ay > ax) {
        use_x = false;
    } else if (linger_live && (linger_dir == LED_MATRIX_DIR_UP || linger_dir == LED_MATRIX_DIR_DOWN)) {
        use_x = false;
    } else {
        use_x = true;
    }

    if (use_x) {
        return (x >= 0) ? LED_MATRIX_DIR_RIGHT : LED_MATRIX_DIR_LEFT;
    }
    return (y >= 0) ? LED_MATRIX_DIR_DOWN : LED_MATRIX_DIR_UP;
}

/* table[mode] if present, otherwise the white gutter band. */
static void led_matrix_pointer_draw_overlay(uint8_t dir) {
    uint8_t mode = bkpd_mode_get_active_id();
    if (mode < ARRAY_SIZE(led_matrix_overlay_by_mode) && led_matrix_overlay_by_mode[mode] != NULL) {
        led_matrix_overlay_by_mode[mode](dir);
        return;
    }
    led_matrix_module_fill_band(dir);
}

bool led_matrix_pointer_apply_overlay(void) {
    int16_t x = 0;
    int16_t y = 0;
    bkpd_get_mouse_movement(&x, &y);

    bool just_expired = false;
    if (x != 0 || y != 0) {
        linger_dir     = led_matrix_pointer_dir_from_delta(x, y);
        linger_started = timer_read32();
        linger_live    = true;
    } else if (linger_live && timer_elapsed32(linger_started) >= LED_MATRIX_POINTER_LINGER_MS) {
        linger_live  = false;
        linger_dir   = LED_MATRIX_DIR_NONE;
        just_expired = true;
    }

    if (linger_live) {
        led_matrix_pointer_draw_overlay(linger_dir);
    }
    return linger_live || just_expired;
}
