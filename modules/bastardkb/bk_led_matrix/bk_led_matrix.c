// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_duck.h"
#include "led_matrix_layers.h"
#include "led_matrix_mods.h"
#include "led_matrix_pointer.h"
#include "led_matrix_motion.h"
#include "led_matrix_layer_anims.h"
#include "transactions.h"
#ifdef COMMUNITY_MODULE_BK_POINTING_DEVICE_ENABLE
#    include "bk_pointing_modes.h"
#endif

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 0); // ykz89: 1.1.0 for pointing_device_task

static uint32_t bklm_last_input_ms;

/* ykz89: bumped on every key event and trackball move on the USB half and sent
 * to the panel half, whose own key processing never runs when it isn't USB. */
static uint8_t bklm_activity;

/* Any key event resets the idle clock used by housekeeping_task_bk_led_matrix. */
void process_records_bk_led_matrix_note_last_input(uint16_t keycode, keyrecord_t *record) {
    bklm_last_input_ms = timer_read32();
    bklm_activity++;
}

bool process_record_bk_led_matrix(uint16_t keycode, keyrecord_t *record) {
    process_records_bk_led_matrix_note_last_input(keycode, record);
    // ykz89: next trackball animation; with Shift, the previous one.
    if (keycode == LED_MATRIX_ANIMATION_NEXT) {
        if (record->event.pressed) {
            bklm_motion_style_set(bklm_motion_cycle_next(bklm_motion_style, get_mods() & MOD_MASK_SHIFT), true);
        }
        return false;
    }
    return true;
}

/* ykz89: bk_pointing_device keeps its mode on the USB half only, so that half
 * sends it to the panel half, along with activity and trackball motion. */
static uint8_t bklm_synced_pointer_mode;

typedef struct __attribute__((packed)) {
    uint8_t mode;
    uint8_t activity;
    uint8_t style; /* trackball animation, picked with LED_MATRIX_ANIMATION_NEXT */
    int16_t dx, dy; /* motion since the last delivered message */
} bklm_sync_t;

static int32_t bklm_motion_x, bklm_motion_y; /* USB half: not yet delivered */

/* Only the USB half sees pointer reports. Feed the animation directly when that is
 * also the panel half, otherwise batch the motion for the sync. */
report_mouse_t pointing_device_task_bk_led_matrix(report_mouse_t mouse_report) {
    if (mouse_report.x != 0 || mouse_report.y != 0) {
        bklm_last_input_ms = timer_read32();
        bklm_activity++;
        if (is_keyboard_left() == (LED_MATRIX_MODULE_ON_LEFT != 0)) {
            bklm_motion_feed(mouse_report.x, mouse_report.y);
        } else {
            bklm_motion_x += mouse_report.x;
            bklm_motion_y += mouse_report.y;
        }
    }
    return mouse_report;
}

static bool bklm_on_panel_half(void) {
    return is_keyboard_left() == (LED_MATRIX_MODULE_ON_LEFT != 0);
}

/* Reset the strip so the first painted frame starts from a known-off pin. */
void keyboard_post_init_bk_led_matrix(void) {
    bklm_last_input_ms = timer_read32();
    bklm_motion_style_load();
    if (bklm_on_panel_half()) {
        bklm_init();
    }
}

/* The sender holds interrupts for ~6 ms, so frames are paced. Indicators are
 * mutually exclusive, most urgent first; each returns false when it has nothing
 * to show. Each fills the panel, so layering them would cut holes in one another.
 * The duck is the resting state. */

static bool strip_powered = true;

void housekeeping_task_bk_led_matrix(void) {
    static uint32_t last_update = 0;
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];

    if (!bklm_on_panel_half()) {
        return;
    }

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

    if (timer_elapsed32(last_update) < (bklm_motion_active() ? LED_MATRIX_MODULE_MOTION_REFRESH_MS : LED_MATRIX_MODULE_REFRESH_MS)) {
        return;
    }
    last_update = timer_read32();

    memset(frame, 0, sizeof(frame));
    // ykz89: a moving trackball animation sits between the modifiers and the
    // layer stack; an idle one (LED_MATRIX_MODULE_MOTION_IDLE) only behind the
    // layer stack, where it takes the duck's place.
    if (!bklm_pointer_paint(frame) && !bklm_draw_active_modifier_names(frame)) {
        if (bklm_motion_moving()) {
            bklm_draw_motion(frame);
        } else if (!bklm_draw_layer_anim(frame) && !bklm_draw_layer_stack(frame) && !bklm_draw_motion(frame)) {
            bklm_draw_bobbing_duck(frame);
        }
    }
    bklm_show(frame);
}

void suspend_power_down_bk_led_matrix(void) {
    static RGB      frame[LED_MATRIX_MODULE_LED_COUNT];
    if (!bklm_on_panel_half()) {
        return; // ykz89: no panel on this half
    }
    memset(frame, 0, sizeof(frame));
    bklm_set_idle_brightness_divisor(1);
    bklm_show(frame);
    strip_powered = false;
}