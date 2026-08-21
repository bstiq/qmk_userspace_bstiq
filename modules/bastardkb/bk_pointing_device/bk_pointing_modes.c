/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Publicw License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include "bk_pointing_modes.h"
#include "bk_pointing_device.h"

#define BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE 6
#define BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_X 40
#define BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_Y 80

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos.h"
#endif

// pointers to different functions for each mode
typedef struct {
    report_mouse_t (*process)(report_mouse_t mouse_report);
    void (*set_active)(bool);
    void (*invert_axis)(bool axis);
    void (*cycle_dpi)(bool forward);
} pointing_mode_t;

static pointing_mode_t modes[] = {
    {.process = NULL, .set_active = bkpd_mode_normal_set_active, .invert_axis = NULL, .cycle_dpi = NULL}, // normal
    {.process = NULL, .set_active = bkpd_mode_sniping_set_active, .invert_axis = NULL, .cycle_dpi = NULL}, // sniping
    {.process = bkpd_mode_dragscroll_process, .set_active = NULL, .invert_axis = NULL, .cycle_dpi = NULL}, // dragscroll
    {.process = bkpd_mode_cursor_process, .set_active = NULL, .invert_axis = NULL, .cycle_dpi = NULL}  // cursor
};

static pointing_mode_t *active_mode = &modes[0]; // reset on keyboard connection

/* -----------------------------------------------------------------------------
            Helper functions for mode management */

void bkpd_mode_set_active(uint8_t id) {
    uint8_t num_modes = sizeof(modes) / sizeof(pointing_mode_t);
    if (id >= 0 && id < num_modes) {
        printf("Setting mode %d active\n", id);
        if(active_mode != &modes[id]) {
            if(active_mode->set_active != NULL) {
                active_mode->set_active(false); // disable current mode nonetheless, only one mode active at a time
            }
            active_mode = &modes[id];
            if(active_mode->set_active != NULL) {
                active_mode->set_active(true);
            }
        }
    }
}

// activate the mode, or go back to normal mode.
void bkpd_mode_toggle_active(uint8_t id) {
    uint8_t num_modes = sizeof(modes) / sizeof(pointing_mode_t);
    if (id < num_modes) {
        // are we already in this mode?
        if (active_mode == &modes[id]) {
            // go back to normal mode
            bkpd_mode_set_active(MODE_NORMAL);
            return;
        }
        // else, we were in another mode
        else {
            // deactivate current mode
            active_mode->set_active(false);
            // activate new mode
            active_mode = &modes[id];
            active_mode->set_active(true);
        }
    }
}

report_mouse_t bkpd_process_active_mode(report_mouse_t mouse_report) {
    if (active_mode->process != NULL) {
        return active_mode->process(mouse_report);
    }
    return mouse_report;
}

/* -----------------------------------------------------------------------------
            Helper functions for current active mode */

void bkpd_mode_current_cycle_dpi(bool forward) {
    if (active_mode->cycle_dpi != NULL) {
        active_mode->cycle_dpi(forward);
    }
}

void bkpd_mode_current_invert_axis(bool axis) {
    if (active_mode->invert_axis != NULL) {
        active_mode->invert_axis(axis);
    }
}

/* -----------------------------------------------------------------------------
            Legacy helpers for backwards compatibility */

void bkpd_mode_legacy_sniping_cycle_dpi(bool forward) {
    modes[MODE_SNIPING].cycle_dpi(forward);
}

/* -----------------------------------------------------------------------------
            Normal mode */

void bkpd_mode_normal_set_active(bool active) {
    printf("Setting normal mode %s\n", active ? "active" : "inactive");
    if (active) {
        pointing_device_set_cpi(bkpd_get_pointer_default_dpi());
    }
    printf("done\n");
}

/* -----------------------------------------------------------------------------
            Sniping mode */

// TODO manage auto mouse layer.....
void bkpd_mode_sniping_set_active(bool active) {
    printf("Setting sniping mode %s\n", active ? "active" : "inactive");
    if (active) {
        pointing_device_set_cpi(bkpd_get_pointer_sniping_dpi());
    }
    printf("done\n");
}

/* -----------------------------------------------------------------------------
            Dragscroll mode */

// TODO: reset buffer on mode change
// TODO invert options
// TODO DPI options
report_mouse_t bkpd_mode_dragscroll_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    buffer_x += mouse_report.x;
    buffer_y += mouse_report.y;
    mouse_report.x = 0;
    mouse_report.y = 0;
    if (abs(buffer_x) > BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE) {
        mouse_report.h = buffer_x > 0 ? 1 : -1;
        buffer_x = 0;
    }
    if (abs(buffer_y) > BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE) {
        mouse_report.v = buffer_y > 0 ? 1 : -1;
        buffer_y = 0;
    }
    return mouse_report;
}

/* -----------------------------------------------------------------------------
            Cursor mode */

// TODO: reset buffer on mode change
// TODO invert options
// TODO DPI options
report_mouse_t bkpd_mode_cursor_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    buffer_x += mouse_report.x;
    buffer_y += mouse_report.y;
    mouse_report.x = 0;
    mouse_report.y = 0;
    if(abs(buffer_x) > BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_X) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
        argos_keycode_tap(buffer_x > 0 ? KC_RIGHT : KC_LEFT);
#else
        tap_code(buffer_x > 0 ? KC_RIGHT : KC_LEFT);
#endif
        buffer_x = 0;
        // only allow to go horizontal or vertical at a time
        buffer_y = 0;
    }
    if(abs(buffer_y) > BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_Y) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
        argos_keycode_tap(buffer_y > 0 ? KC_DOWN : KC_UP);
#else
        tap_code(buffer_y > 0 ? KC_DOWN : KC_UP);
#endif
        buffer_y = 0;
        // only allow to go horizontal or vertical at a time
        buffer_x = 0;
    }
    return mouse_report;
}