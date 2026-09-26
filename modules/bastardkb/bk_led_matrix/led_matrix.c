// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_layers.h"
#include "led_matrix_mods.h"
#include "led_matrix_pointer.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

/* Reset the strip so the first painted frame starts from a known-off pin. */
void keyboard_post_init_led_matrix(void) {
    bklm_reset_strip();
}

/* The sender holds interrupts for ~6 ms, so frames are paced.
 * Clear first: the gutter and unused well must stay dark. */
void housekeeping_task_led_matrix(void) {
    static uint32_t last_update = 0;
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];

    if (timer_elapsed32(last_update) < LED_MATRIX_MODULE_REFRESH_MS) {
        return;
    }
    last_update = timer_read32();

    /* Pointer, then mods, then the layer digit. Later draws win, so
     * the number stays readable over the caps tile in the top-left. */
    memset(frame, 0, sizeof(frame));
    bklm_draw_pointer_mode_icon(frame);
    bklm_draw_active_modifier_icons(frame);
    bklm_draw_active_layer_digit(frame);
    bklm_send_frame_to_strip(frame);
}
