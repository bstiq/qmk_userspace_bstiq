// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_duck.h"
#include "led_matrix_layers.h"
#include "led_matrix_mods.h"
#include "led_matrix_pointer.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

static uint32_t bklm_last_input_ms;

/* Any key event resets the idle clock used by housekeeping_task_bk_led_matrix. */
void process_records_bk_led_matrix_note_last_input(uint16_t keycode, keyrecord_t *record) {
    // unused(keycode);
    // unused(record);
    bklm_last_input_ms = timer_read32();
}

bool process_record_bk_led_matrix(uint16_t keycode, keyrecord_t *record) {
    process_records_bk_led_matrix_note_last_input(keycode, record);
    return true;
}

/* Reset the strip so the first painted frame starts from a known-off pin. */
void keyboard_post_init_bk_led_matrix(void) {
    bklm_last_input_ms = timer_read32();
    bklm_init();
}

/* The sender holds interrupts for ~6 ms, so frames are paced.
 * Clear first: any cell no indicator claims must stay dark.
 *
 * The indicators are mutually exclusive, most urgent first: each returns false
 * when it has nothing to say and the next one gets the frame. A pointer mode,
 * a held modifier and the layer stack each fill the panel on their own, so
 * layering them would only cut holes in one another. The duck is last because
 * it is the resting state, not an indicator. */

static bool strip_powered = true;

void housekeeping_task_bk_led_matrix(void) {
    static uint32_t last_update = 0;
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];

    const uint32_t idle_ms = timer_elapsed32(bklm_last_input_ms);

    if (idle_ms >= LED_MATRIX_MODULE_OFF_MS) {
        if (strip_powered) {
            memset(frame, 0, sizeof(frame));
            bklm_set_idle_brightness_divisor(1);
            bklm_show(frame);
            strip_powered = false;
        }
        return;
    }

    strip_powered = true;
    bklm_set_idle_brightness_divisor(idle_ms >= LED_MATRIX_MODULE_DIM_MS ? 2 : 1);

    if (timer_elapsed32(last_update) < LED_MATRIX_MODULE_REFRESH_MS) {
        return;
    }
    last_update = timer_read32();

    memset(frame, 0, sizeof(frame));
    if (!bklm_pointer_paint(frame) && !bklm_draw_active_modifier_names(frame) && !bklm_draw_layer_stack(frame)) {
        bklm_draw_bobbing_duck(frame);
    }
    bklm_show(frame);
}

void suspend_power_down_bk_led_matrix(void) {
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];
    memset(frame, 0, sizeof(frame));
    bklm_set_idle_brightness_divisor(1);
    bklm_show(frame);
    strip_powered = false;
}