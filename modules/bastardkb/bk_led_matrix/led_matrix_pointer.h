// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include "led_matrix_icons.h"

enum {
    LED_MATRIX_DIR_NONE = 0,
    LED_MATRIX_DIR_RIGHT,
    LED_MATRIX_DIR_LEFT,
    LED_MATRIX_DIR_UP,
    LED_MATRIX_DIR_DOWN,
};

/* Logic only: which logo belongs to the current pointer mode.
 * Returns NULL when there is no pointing module, or the mode is Normal. */
const led_matrix_icon_t *led_matrix_pointer_active_icon(void);

/* Poll movement, keep a 200 ms linger, and paint the overlay if live.
 * Returns true when the overlay is live or just expired. No-op without pointing. */
bool led_matrix_pointer_apply_overlay(void);
