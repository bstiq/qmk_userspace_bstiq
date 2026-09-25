// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_module.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

/* Prepares the matrix pin before the first frame. */
void keyboard_post_init_led_matrix(void) {
    bklm_init();
}

/* Sends a solid red frame at LED_MATRIX_MODULE_REFRESH_MS. */
void housekeeping_task_led_matrix(void) {
    static uint32_t last_update = 0;
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];

    if (timer_elapsed32(last_update) < LED_MATRIX_MODULE_REFRESH_MS) {
        return;
    }
    last_update = timer_read32();

    for (uint16_t i = 0; i < LED_MATRIX_MODULE_LED_COUNT; i++) {
        frame[i].r = 255;
        frame[i].g = 0;
        frame[i].b = 0;
    }
    bklm_show(frame);
}
