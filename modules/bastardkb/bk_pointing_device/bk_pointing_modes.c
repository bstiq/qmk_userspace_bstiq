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
#include "math.h"

#define BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE 6
#define BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_X 40
#define BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_Y 80

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos.h"
#endif

#define BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI 400
#define BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP 200
#define BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI 200
#define BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP 100
#define BK_POINTING_DEVICE_DRAGSCROLL_DPI 100

#define BK_POINTING_DEVICE_MAX_DPI_BYTES 4
#define BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES 2

extern bkpd_config_t g_bkpd_config;

// pointers to different functions for each mode
typedef struct {
    uint8_t id;
    report_mouse_t (*process)(report_mouse_t mouse_report);
    void (*set_active)(bool);
    void (*invert_axis)(bool axis);
} pointing_mode_t;

static pointing_mode_t modes[] = {
    {.id = MODE_NORMAL, .process = NULL, .set_active = NULL, .invert_axis = NULL},
    {.id = MODE_SNIPING, .process = NULL, .set_active = NULL, .invert_axis = NULL},
    {.id = MODE_DRAGSCROLL, .process = bkpd_mode_dragscroll_process, .set_active = NULL, .invert_axis = NULL},
    {.id = MODE_CURSOR, .process = bkpd_mode_cursor_process, .set_active = NULL, .invert_axis = NULL}
};

static pointing_mode_t *active_mode = &modes[0]; // reset on keyboard connection

/* -----------------------------------------------------------------------------
            Helper functions for mode management */

void bkpd_modes_init(void) {
    g_bkpd_config.modes_config[MODE_NORMAL].max_dpi_steps = pow(2, BK_POINTING_DEVICE_MAX_DPI_BYTES);
    g_bkpd_config.modes_config[MODE_SNIPING].max_dpi_steps = pow(2, BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES);
    g_bkpd_config.modes_config[MODE_DRAGSCROLL].max_dpi_steps = pow(2, BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES); // TODO
    g_bkpd_config.modes_config[MODE_CURSOR].max_dpi_steps = pow(2, BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES);// TODO

    g_bkpd_config.modes_config[MODE_NORMAL].dpi_per_step = BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP;
    g_bkpd_config.modes_config[MODE_SNIPING].dpi_per_step = BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP;
    g_bkpd_config.modes_config[MODE_DRAGSCROLL].dpi_per_step = BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP;// TODO
    g_bkpd_config.modes_config[MODE_CURSOR].dpi_per_step = BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP;// TODO

    g_bkpd_config.modes_config[MODE_NORMAL].minimum_dpi = BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI;
    g_bkpd_config.modes_config[MODE_SNIPING].minimum_dpi = BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI;
    g_bkpd_config.modes_config[MODE_DRAGSCROLL].minimum_dpi = BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI; // TODO
    g_bkpd_config.modes_config[MODE_CURSOR].minimum_dpi = BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI; // TODO

}

void bkpd_activate_mode_if_pressed_normal_otherwise(keyrecord_t *record, uint8_t mode) {
    if (record->event.pressed) {
        bkpd_mode_set_active(mode);
    } else {
        bkpd_mode_set_active(MODE_NORMAL);
    }
}

uint8_t bkpd_mode_get_active_id(void) {
    return active_mode->id;
}

void bkpd_mode_set_active(uint8_t id) {
    uint8_t num_modes = sizeof(modes) / sizeof(pointing_mode_t);
    if (id >= 0 && id < num_modes) {
        if(active_mode != &modes[id]) {
            if(active_mode->set_active != NULL) {
                active_mode->set_active(false); // disable current mode nonetheless, only one mode active at a time
            }
            active_mode = &modes[id];
            if(active_mode->set_active != NULL) {
                active_mode->set_active(true);
            }
        }
        // affect DPI
        bkpd_mode_current_affect_dpi();
    }
}

// activate the mode, or go back to normal mode.
void bkpd_mode_toggle_active(uint8_t mode_id) {
    uint8_t num_modes = sizeof(modes) / sizeof(pointing_mode_t);
    if (mode_id < num_modes) {
        // are we already in this mode?
        if (active_mode == &modes[mode_id]) {
            // go back to normal mode
            bkpd_mode_set_active(MODE_NORMAL);
            return;
        }
        // else, we were in another mode
        else {
            bkpd_mode_set_active(mode_id);
        }
    }
}

report_mouse_t bkpd_process_active_mode(report_mouse_t mouse_report) {
    // invert x/y if needed, regardless of following processing
    if(bkpd_mode_get_invert(active_mode->id, 0)) {
        mouse_report.x = -mouse_report.x;
    }
    if(bkpd_mode_get_invert(active_mode->id, 1)) {
        mouse_report.y = -mouse_report.y;
    }
    if (active_mode->process != NULL) {
        return active_mode->process(mouse_report);
    }
    return mouse_report;
}

void bkpd_mode_cycle_dpi(uint8_t mode_id, bool forward) {
    // compare bytes etc - TODO
    // get the current mode's max dpi
    uint8_t max_dpi_steps = g_bkpd_config.modes_config[mode_id].max_dpi_steps;

    // get the current dpi step
    uint8_t current_dpi_step = g_bkpd_config.modes_config[mode_id].current_dpi_step;
    // calculate the new dpi step, could be higher than 256 (meh)    
    uint16_t new_dpi_step = current_dpi_step + (forward ? 1 : -1);

    // if the new dpi step is greater than the max dpi steps, wrap around with modulo
    new_dpi_step = new_dpi_step % max_dpi_steps;

    // save the new dpi step
    g_bkpd_config.modes_config[mode_id].current_dpi_step = new_dpi_step;

    // affect in EEPROM
    write_bkpd_config_to_eeprom();

    // calculate new DPI
    uint16_t new_dpi = new_dpi_step * g_bkpd_config.modes_config[mode_id].dpi_per_step + BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI;
    printf("new_dpi: %d\n", new_dpi);
    // if mode_id is active, update current DPI
    if (active_mode == &modes[mode_id]) {
        pointing_device_set_cpi(new_dpi);
    }
}

void bkpd_mode_set_invert(uint8_t mode_id, uint8_t axis_index, bool invert) {
    if(mode_id > MODE_LAST)
        return;
    if(axis_index > 1)
        return;
    printf("setting invert for mode_id: %d, axis_index: %d, invert: %d\n", mode_id, axis_index, invert);
    if(axis_index == 0) {
        g_bkpd_config.modes_config[mode_id].invert_x = invert;
    }
    else {
        g_bkpd_config.modes_config[mode_id].invert_y = invert;
    }
    write_bkpd_config_to_eeprom();
}

uint16_t bkpd_mode_get_minimum_dpi(uint8_t mode_id) {
    if(mode_id > MODE_LAST)
        return 0;
    return g_bkpd_config.modes_config[mode_id].minimum_dpi;
}

uint16_t bkpd_mode_get_max_dpi(uint8_t mode_id) {
    if(mode_id > MODE_LAST)
        return 0;
    return g_bkpd_config.modes_config[mode_id].max_dpi_steps * g_bkpd_config.modes_config[mode_id].dpi_per_step + g_bkpd_config.modes_config[mode_id].minimum_dpi;
}

bool bkpd_mode_get_invert(uint8_t mode_id, uint8_t axis_index) {
    if(mode_id > MODE_LAST)
        return false;
    if(axis_index > 1)
        return false;
    if(axis_index == 0) {
        return g_bkpd_config.modes_config[mode_id].invert_x;
    }
    else {
        return g_bkpd_config.modes_config[mode_id].invert_y;
    }
}

uint16_t bkpd_mode_get_dpi_per_step(uint8_t mode_id) {
    if(mode_id > MODE_LAST)
        return 0;
    return g_bkpd_config.modes_config[mode_id].dpi_per_step;
}


/* -----------------------------------------------------------------------------
            Helper functions for current active mode */

void bkpd_mode_current_cycle_dpi(bool forward) {
    bkpd_mode_cycle_dpi(active_mode->id, forward);
}

void bkpd_mode_current_invert_axis(bool axis) {
    if (active_mode->invert_axis != NULL) {
        active_mode->invert_axis(axis);
    }
}

uint16_t bkpd_mode_calculate_dpi_from_bytes(uint8_t mode_id) {
    if(mode_id > MODE_LAST)
        return 0;

    return g_bkpd_config.modes_config[mode_id].current_dpi_step * g_bkpd_config.modes_config[mode_id].dpi_per_step + g_bkpd_config.modes_config[mode_id].minimum_dpi;
}

void bkpd_mode_affect_dpi(uint8_t mode_id) {
    if(mode_id > MODE_LAST)
        return;

    uint16_t new_dpi = bkpd_mode_calculate_dpi_from_bytes(mode_id);
    if(new_dpi != pointing_device_get_cpi()) {
        pointing_device_set_cpi(new_dpi);
    }
}

void bkpd_mode_affect_dpi_from_bytes(uint8_t mode_id, uint16_t new_dpi) {
    if(mode_id > MODE_LAST)
        return;

    // we can affect new DPI directly, no need to cycle through steps manually
    g_bkpd_config.modes_config[mode_id].current_dpi_step = new_dpi / g_bkpd_config.modes_config[mode_id].dpi_per_step;
    write_bkpd_config_to_eeprom();
    pointing_device_set_cpi(new_dpi);

}

void bkpd_mode_current_affect_dpi(void) {
    bkpd_mode_affect_dpi(active_mode->id);
}

static void accumulate_buffer(int16_t *buffer_x, int16_t *buffer_y, int8_t dx, int8_t dy) {
    *buffer_x += dx;
    *buffer_y += dy;
}

/* -----------------------------------------------------------------------------
            Legacy helpers for backwards compatibility */

void bkpd_mode_legacy_sniping_cycle_dpi(bool forward) {
    bkpd_mode_cycle_dpi(MODE_SNIPING, forward);
}

/* -----------------------------------------------------------------------------
            Normal mode */

/* -----------------------------------------------------------------------------
            Sniping mode */

/* -----------------------------------------------------------------------------
            Dragscroll mode */

// TODO: reset buffer on mode change
// TODO invert options
// TODO DPI options
// Helper function to accumulate deltas in buffer

report_mouse_t bkpd_mode_dragscroll_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
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
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
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