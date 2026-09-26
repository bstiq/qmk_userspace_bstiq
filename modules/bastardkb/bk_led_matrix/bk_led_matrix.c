// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_layers.h"
#include "led_matrix_mods.h"
#include "led_matrix_pointer.h"
#include "led_matrix_bar.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

/* Reset the strip so the first painted frame starts from a known-off pin. */
void keyboard_post_init_bk_led_matrix(void) {
    bklm_init();
}

/* The sender holds interrupts for ~6 ms, so frames are paced.
 * Clear first: the gutter and unused well must stay dark.
 * Pointer mode owns the well; otherwise mods, Argos bar column, then layer digit. */
void housekeeping_task_bk_led_matrix(void) {
    static uint32_t last_update = 0;
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];

    if (timer_elapsed32(last_update) < LED_MATRIX_MODULE_REFRESH_MS) {
        return;
    }
    const uint32_t now_ms = timer_read32();
    last_update           = now_ms;

    memset(frame, 0, sizeof(frame));
    if (!bklm_pointer_paint(frame)) {
        bklm_draw_active_modifier_icons(frame);
        bklm_draw_left_bar_column(frame, now_ms);
        bklm_layers_paint(frame);
    }
    bklm_show(frame);
}
