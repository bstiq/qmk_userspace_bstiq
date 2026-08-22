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
#define BK_POINTING_DEVICE_BRIGHTNESS_BUFFER_SIZE 30
#define BK_POINTING_DEVICE_ZOOM_BUFFER_SIZE 20
#define BK_POINTING_DEVICE_VOLUME_BUFFER_SIZE 20

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos.h"
#endif

#define BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI 400
#define BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP 200
#define BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI 100
#define BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP 100
#define BK_POINTING_DEVICE_DRAGSCROLL_DPI 100

#define BK_POINTING_DEVICE_MAX_DPI_BYTES 4
#define BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES 4

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
     {.id = MODE_CURSOR, .process = bkpd_mode_cursor_process, .set_active = NULL, .invert_axis = NULL},
     {.id = MODE_BRIGHTNESS, .process = bkpd_mode_brightness_process, .set_active = NULL, .invert_axis = NULL},
     {.id = MODE_ZOOM, .process = bkpd_mode_zoom_process, .set_active = NULL, .invert_axis = NULL},
     {.id = MODE_VOLUME, .process = bkpd_mode_volume_process, .set_active = NULL, .invert_axis = NULL}
    };

static pointing_mode_t *active_mode = &modes[0]; // reset on keyboard connection

// manually defined from introspection
// TODO figure out a better way to do this
mode_map_t g_mode_map[] = {
    {.keycode = 0x7E04, .mode = MODE_SNIPING},
    {.keycode = 0x7E06, .mode = MODE_DRAGSCROLL},
    {.keycode = 0x7E08, .mode = MODE_CURSOR},
    {.keycode = 0x7E0A, .mode = MODE_BRIGHTNESS},
    {.keycode = 0x7E0C, .mode = MODE_ZOOM},
    {.keycode = 0x7E0E, .mode = MODE_VOLUME},
};

/* -----------------------------------------------------------------------------
            Helper functions for mode management */

void bkpd_modes_init(void) {

    // Init to default values
    for(int i = 0; i < MODE_LAST; i++) {
        g_bkpd_config.modes_config[i].max_dpi_steps     = pow(2, BK_POINTING_DEVICE_MAX_SNIPING_DPI_BYTES);
        g_bkpd_config.modes_config[i].dpi_per_step     = BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP;
        g_bkpd_config.modes_config[i].minimum_dpi     = BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI;
    }

    // edit specific values
    g_bkpd_config.modes_config[MODE_NORMAL].max_dpi_steps     = pow(2, BK_POINTING_DEVICE_MAX_DPI_BYTES);
    g_bkpd_config.modes_config[MODE_NORMAL].dpi_per_step     = BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP;
    g_bkpd_config.modes_config[MODE_NORMAL].minimum_dpi     = BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI;
}

uint16_t bkpd_get_highest_mode_keycode(void) {
    uint16_t highest_keycode = 0;
    for(int i = 0; i < sizeof(g_mode_map) / sizeof(mode_map_t); i++) {
        if(g_mode_map[i].keycode > highest_keycode) {
            highest_keycode = g_mode_map[i].keycode;
        }
    }
    return highest_keycode;
}

uint16_t bkpd_get_lowest_mode_keycode(void) {
    uint16_t lowest_keycode = bkpd_get_highest_mode_keycode();
    for(int i = 0; i < sizeof(g_mode_map) / sizeof(mode_map_t); i++) {
        if(g_mode_map[i].keycode < lowest_keycode) {
            lowest_keycode = g_mode_map[i].keycode;
        }
    }
    return lowest_keycode;
}

uint8_t bkpd_mode_from_keycode(uint16_t keycode) {
    for(int i = 0; i < sizeof(g_mode_map) / sizeof(mode_map_t); i++) {
        if(g_mode_map[i].keycode == keycode || // regular or toggle
            g_mode_map[i].keycode + 1 == keycode) {
            return g_mode_map[i].mode;
        }
    }
    return -1;
}

uint8_t bkpd_mode_get_active_id(void) {
    return active_mode->id;
}

bool bkpd_mode_is_valid(uint8_t mode_id) {
    // manually test for mode 0
    if(mode_id == MODE_NORMAL) {
        return true;
    }
    // otherwise, test in map
    for(int i = 0; i < sizeof(g_mode_map) / sizeof(mode_map_t); i++) {
        if(g_mode_map[i].mode == mode_id) {
            return true;
        }
    }
    return false;
}

void bkpd_mode_set_active(uint8_t id) {
    // test if the mode is valid: search in map
    if (bkpd_mode_is_valid(id)) {
        if (active_mode != &modes[id]) {
            if (active_mode->set_active != NULL) {
                active_mode->set_active(false); // disable current mode nonetheless, only one mode active at a time
            }
            active_mode = &modes[id];
            if (active_mode->set_active != NULL) {
                active_mode->set_active(true);
            }
        }
        // affect DPI
        bkpd_mode_apply_dpi(id);
    }
}

// activate the mode, or go back to normal mode.
void bkpd_mode_toggle_active(uint8_t mode_id) {
    if (bkpd_mode_is_valid(mode_id)) {
        // are we already in this mode?
        if (active_mode == &modes[mode_id]) {
            // go back to normal mode
            bkpd_mode_set_active(MODE_NORMAL);
            return;
        }
        // else, we were in another mode - we can then switch up the mode.
        else {
            bkpd_mode_set_active(mode_id);
        }
    }
}

report_mouse_t bkpd_process_active_mode(report_mouse_t mouse_report) {
    // invert x/y if needed, regardless of following processing
    if (bkpd_mode_get_invert(active_mode->id, 0)) {
        mouse_report.x = -mouse_report.x;
    }
    if (bkpd_mode_get_invert(active_mode->id, 1)) {
        mouse_report.y = -mouse_report.y;
    }
    if (active_mode->process != NULL) {
        return active_mode->process(mouse_report);
    }
    return mouse_report;
}

// used by argos
void bkpd_mode_cycle_dpi(uint8_t mode_id, bool forward) {
    // compare bytes etc - TODO
    // get the current mode's max dpi
    uint8_t max_dpi_steps = g_bkpd_config.modes_config[mode_id].max_dpi_steps;

    // get the current dpi step
    uint8_t current_dpi_step = bkpd_mode_get_dpi_per_step(mode_id);
    // calculate the new dpi step, could be higher than 256 (meh)
    uint16_t new_dpi_step = current_dpi_step + (forward ? 1 : -1);

    // if the new dpi step is greater than the max dpi steps, wrap around with modulo
    new_dpi_step = new_dpi_step % max_dpi_steps;

    // save the new dpi step
    g_bkpd_config.modes_config[mode_id].current_dpi_step = new_dpi_step;

    // affect in EEPROM
    write_bkpd_config_to_eeprom();

    // calculate new DPI
    uint16_t new_dpi = new_dpi_step * bkpd_mode_get_dpi_per_step(mode_id) + BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI;
    printf("new_dpi: %d\n", new_dpi);
    // if mode_id is active, update current DPI
    if (active_mode == &modes[mode_id]) {
        pointing_device_set_cpi(new_dpi);
    }
}

void bkpd_mode_set_invert(uint8_t mode_id, uint8_t axis_index, bool invert) {
    if(!bkpd_mode_is_valid(mode_id)) return;
    if (axis_index > 1) return;
    printf("setting invert for mode_id: %d, axis_index: %d, invert: %d\n", mode_id, axis_index, invert);
    if (axis_index == 0) {
        g_bkpd_config.modes_config[mode_id].invert_x = invert;
    } else {
        g_bkpd_config.modes_config[mode_id].invert_y = invert;
    }
    write_bkpd_config_to_eeprom();
}

uint16_t bkpd_mode_get_minimum_dpi(uint8_t mode_id) {
    if(!bkpd_mode_is_valid(mode_id)) return 0;
    return g_bkpd_config.modes_config[mode_id].minimum_dpi;
}

uint16_t bkpd_mode_get_max_dpi(uint8_t mode_id) {
    if(!bkpd_mode_is_valid(mode_id)) return 0;
    return g_bkpd_config.modes_config[mode_id].max_dpi_steps * bkpd_mode_get_dpi_per_step(mode_id) + bkpd_mode_get_minimum_dpi(mode_id);
}

bool bkpd_mode_get_invert(uint8_t mode_id, uint8_t axis_index) {
    if(!bkpd_mode_is_valid(mode_id)) return false;
    if (axis_index > 1) return false;
    if (axis_index == 0) {
        return g_bkpd_config.modes_config[mode_id].invert_x;
    } else {
        return g_bkpd_config.modes_config[mode_id].invert_y;
    }
}

uint16_t bkpd_mode_get_dpi_per_step(uint8_t mode_id) {
    if(!bkpd_mode_is_valid(mode_id)) return 0;
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

uint16_t bkpd_mode_get_dpi(uint8_t mode_id) {
    if(!bkpd_mode_is_valid(mode_id)) return 0;
    return g_bkpd_config.modes_config[mode_id].current_dpi_step * bkpd_mode_get_dpi_per_step(mode_id) + bkpd_mode_get_minimum_dpi(mode_id);
}

void bkpd_mode_apply_dpi(uint8_t mode_id) {
    if(!bkpd_mode_is_valid(mode_id)) return;

    uint16_t new_dpi = bkpd_mode_get_dpi(mode_id);
    printf("new_dpi from mode_id: %d is %d\n", mode_id, new_dpi);
    
    // clamp
    if (new_dpi > bkpd_mode_get_max_dpi(mode_id)) new_dpi = bkpd_mode_get_max_dpi(mode_id);
    if (new_dpi < bkpd_mode_get_minimum_dpi(mode_id)) new_dpi = bkpd_mode_get_minimum_dpi(mode_id);

    if (new_dpi == pointing_device_get_cpi()) return;

    printf("new_dpi after clamp: %d\n", new_dpi);

    pointing_device_set_cpi(new_dpi);
}

void bkpd_mode_change_dpi(uint8_t mode_id, uint16_t new_dpi) {
    if(!bkpd_mode_is_valid(mode_id)) return;
    // calculate step from new DPI
    // essentially the inverse of the formula in bkpd_mode_get_dpi()
    uint8_t new_dpi_step = (new_dpi - bkpd_mode_get_minimum_dpi(mode_id)) / bkpd_mode_get_dpi_per_step(mode_id);

    g_bkpd_config.modes_config[mode_id].current_dpi_step = new_dpi_step;
    printf("applied dpi from bytes: mode_id: %d, new_dpi_step: %d\n", mode_id, new_dpi_step);
    write_bkpd_config_to_eeprom();

    // if active mode (eg. Normal), then apply immediately
    if (active_mode->id == mode_id) {
        bkpd_mode_apply_dpi(mode_id);
    }
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

report_mouse_t bkpd_mode_dragscroll_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
    mouse_report.x = 0;
    mouse_report.y = 0;
    if (abs(buffer_x) > BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE) {
        mouse_report.h = buffer_x > 0 ? 1 : -1;
        buffer_x       = 0;
    }
    if (abs(buffer_y) > BK_POINTING_DEVICE_DRAGSCROLL_BUFFER_SIZE) {
        mouse_report.v = buffer_y > 0 ? 1 : -1;
        buffer_y       = 0;
    }
    return mouse_report;
}

/* -----------------------------------------------------------------------------
            Cursor mode */

report_mouse_t bkpd_mode_cursor_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
    mouse_report.x = 0;
    mouse_report.y = 0;
    if (abs(buffer_x) > BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_X) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
        argos_keycode_tap(buffer_x > 0 ? KC_RIGHT : KC_LEFT);
#else
        tap_code(buffer_x > 0 ? KC_RIGHT : KC_LEFT);
#endif
        buffer_x = 0;
        // only allow to go horizontal or vertical at a time
        buffer_y = 0;
    }
    if (abs(buffer_y) > BK_POINTING_DEVICE_CURSOR_BUFFER_SIZE_Y) {
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

/* -----------------------------------------------------------------------------
            Brightness mode */

report_mouse_t bkpd_mode_brightness_process(report_mouse_t mouse_report) {
#if defined(RGBLIGHT_ENABLE) || defined(RGB_MATRIX_ENABLE)
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
    mouse_report.x = 0; 
    mouse_report.y = 0;
    if (abs(buffer_y) > BK_POINTING_DEVICE_BRIGHTNESS_BUFFER_SIZE) {
        (buffer_y > 0)? rgb_matrix_decrease_val() : rgb_matrix_increase_val();
        buffer_y = 0;
    }
    return mouse_report;
#endif
}

/* -----------------------------------------------------------------------------
            Zoom mode */

report_mouse_t bkpd_mode_zoom_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
    mouse_report.x = 0;
    mouse_report.y = 0;
    // tap Control + plus or minus
    if (abs(buffer_y) > BK_POINTING_DEVICE_ZOOM_BUFFER_SIZE) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
        argos_keycode_down(KC_LCTL);
        argos_keycode_tap(buffer_y < 0 ? KC_PLUS : KC_MINUS);
        argos_keycode_up(KC_LCTL);
#else
        register_code(KC_LCTL);
        tap_code(buffer_y < 0 ? KC_PLUS : KC_MINUS);
        unregister_code(KC_LCTL);
#endif
        buffer_y = 0;
    }
    return mouse_report;
}

/* -----------------------------------------------------------------------------
            Volume mode */

report_mouse_t bkpd_mode_volume_process(report_mouse_t mouse_report) {
    static int16_t buffer_x = 0;
    static int16_t buffer_y = 0;
    accumulate_buffer(&buffer_x, &buffer_y, mouse_report.x, mouse_report.y);
    mouse_report.x = 0;
    mouse_report.y = 0;
    // tap volume keys
    if (abs(buffer_y) > BK_POINTING_DEVICE_VOLUME_BUFFER_SIZE) {
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
        argos_keycode_tap(buffer_y < 0 ? KC_VOLU : KC_VOLD);
#else
        tap_code(buffer_y < 0 ? KC_VOLU : KC_VOLD);
#endif
        buffer_y = 0;
    }
    return mouse_report;
}