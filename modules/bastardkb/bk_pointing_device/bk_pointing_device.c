/*
 * Copyright 2020 Christopher Courtney <drashna@live.com> (@drashna)
 * Copyright 2021 Quentin LEBASTARD <bstkbd@gmail.com>
 * Copyright 2021 Charly Delay <charly@codesink.dev> (@0xcharly)
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

/*
   Generic pointing device configuration and features.
*/

#include QMK_KEYBOARD_H
#include "bk_pointing_device.h"
#include "bk_pointing_modes.h"
#include "transactions.h"
#include <string.h>
#include "math.h"
#include "introspection.h"

#ifdef CONSOLE_ENABLE
#    include "print.h"
#endif // CONSOLE_ENABLE

#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#    include "argos.h"
#    include "argos_rgb.h"
#endif

#ifdef POINTING_DEVICE_DRIVER_digitizer
#    include "digitizer.h"
#endif

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

bkpd_config_t g_bkpd_config                 = {0};
static int8_t changing_dpi_settings_for_mode = -1;

/**
 * \brief Set the value of `config` from EEPROM.
 *
 * Note that `is_dragscroll_enabled` and `is_sniping_enabled` are purposefully
 * ignored since we do not want to persist this state to memory.  In practice,
 * this state is always written to maximize write-performances.  Therefore, we
 * explicitly set them to `false` in this function.
 */
static void read_bkpd_config_from_eeprom(void) {
// TODO: replace with per-module memory management
#if defined(BK_HAS_POINTING_DEVICE) && defined(COMMUNITY_MODULE_ARGOS_ENABLE)
    argos_read_eeprom(ARGOS_OFFSET_POINTER_CONFIG, &g_bkpd_config, sizeof(bkpd_config_t));
#else
    g_bkpd_config.raw = eeconfig_read_kb() & 0xff;
#endif
    // TODO
    // g_bkpd_config.is_dragscroll_enabled = false;
    // g_bkpd_config.is_sniping_enabled    = false;
}

/**
 * \brief Save the value of `config` to eeprom.
 *
 * Note that all values are written verbatim, including whether drag-scroll
 * and/or sniper mode are enabled.  `read_bkpd_config_from_eeprom(…)`
 * resets these 2 values to `false` since it does not make sense to persist
 * these across reboots of the board.
 */
void write_bkpd_config_to_eeprom(void) {
// TODO: replace with per-module memory management
#if defined(BK_HAS_POINTING_DEVICE) && defined(COMMUNITY_MODULE_ARGOS_ENABLE)
    argos_write_eeprom(ARGOS_OFFSET_POINTER_CONFIG, &g_bkpd_config, sizeof(bkpd_config_t));
#else
    eeconfig_update_kb(g_bkpd_config.raw);
#endif
}

bool bkpd_dispatch_command(const uint8_t command_id, uint8_t **command_data) {
    bool send_data = true;
#if defined(COMMUNITY_MODULE_ARGOS_ENABLE)
    switch (command_id) {
        case argos_id_get_pointing_device_info: {
            bkpd_build_pointing_device_info_command_data(command_data);
            break;
        }
        case argos_id_set_dpi: {
            // uint16_t new_dpi = (*command_data)[0] | ((*command_data)[1] << 8);
            // TODO
            // bkpd_set_pointer_default_dpi(new_dpi);
            break;
        }
        case argos_id_set_sniping_dpi: {
            // uint16_t new_dpi = (*command_data)[0] | ((*command_data)[1] << 8);
            // TODO
            // bkpd_set_pointer_sniping_dpi(new_dpi);
            break;
        }
        case argos_id_set_auto_mouse_layer_enabled: {
            bkpd_set_auto_mouse_layer_enabled((*command_data)[0]);
            break;
        }
        case argos_id_set_auto_precision_on_mouse_layer_enabled: {
            bkpd_set_auto_precision_on_mouse_layer_enabled((*command_data)[0]);
            break;
        }
        case argos_id_set_axis_invert: {
            // TODO +++ change axis_index to 2 bytes, and implement 2 different protocols in Webapp.
            // const uint8_t axis_index = (*command_data)[0];
            // const bool    invert     = (*command_data)[1];
            // if (axis_index == 0) {
            //     bkpd_set_dragscroll_axis_invert_x(invert);
            // } else if (axis_index == 1) {
            //     bkpd_set_dragscroll_axis_invert_y(invert);
            // }
            break;
        }
        case argos_id_set_dragscroll_dpi: {
            // TODO
            break;
        }
        default:
            break;
    }
#endif
    return send_data;
}

void bkpd_build_pointing_device_info_command_data(uint8_t **command_data) {
    (*command_data)[0] = pointing_device_type_unknown;
#ifdef BK_HAS_POINTING_DEVICE
#    if defined(POINTING_DEVICE_DRIVER_pmw3360) // Charybdis / Dilemma trackball
    (*command_data)[0] = pointing_device_type_trackball;
#    elif defined(POINTING_DEVICE_DRIVER_digitizer)           // Dilemma v3 / procyon
    (*command_data)[0] = pointing_device_type_trackpad_procyon;
#    elif defined(POINTING_DEVICE_DRIVER_cirque_pinnacle_spi) // Dilemma v2 / cirque
    (*command_data)[0] = pointing_device_type_trackpad_cirque;
#    endif
    if ((*command_data)[0] != pointing_device_type_unknown) {
        // pointing dpi is up to 400+16*200 = 3600, 2 bytes
        (*command_data)[1] = bkpd_get_pointer_default_dpi() & 0xFF;
        (*command_data)[2] = (bkpd_get_pointer_default_dpi() >> 8) & 0xFF;
        // minimum default DPI is 400, 2 bytes
        uint16_t minimum_default_dpi = bkpd_get_minimum_default_dpi();
        (*command_data)[3]           = minimum_default_dpi & 0xFF;
        (*command_data)[4]           = (minimum_default_dpi >> 8) & 0xFF;
        // default DPI config step is 200, so one byte, but we use 2 just in
        // case
        uint16_t default_dpi_config_step = bkpd_get_default_dpi_config_step();
        (*command_data)[5]               = default_dpi_config_step & 0xFF;
        (*command_data)[6]               = (default_dpi_config_step >> 8) & 0xFF;
        // sniping DPI is up to 200+4*100 = 600, 2 bytes
        (*command_data)[7] = bkpd_get_pointer_sniping_dpi() & 0xFF;
        (*command_data)[8] = (bkpd_get_pointer_sniping_dpi() >> 8) & 0xFF;
        // mininmum sniping dpi is 200, but ue use 2 bytes ju) in case
        uint16_t minimum_sniping_dpi = bkpd_get_minimum_sniping_dpi();
        (*command_data)[9]           = minimum_sniping_dpi & 0xFF;
        (*command_data)[10]          = (minimum_sniping_dpi >> 8) & 0xFF;
        // sniping DPI config step is 100, so one byte, but we use 2 just in
        // case
        uint16_t sniping_dpi_config_step = bkpd_get_sniping_dpi_config_step();
        (*command_data)[11]              = sniping_dpi_config_step & 0xFF;
        (*command_data)[12]              = (sniping_dpi_config_step >> 8) & 0xFF;
        // pointing DPI max steps is 16, so one byte is plenty
        // this is hardcoded here as we can't read it from dilemma.c (private
        // config structure dilemma_config_t)
        (*command_data)[13] = 16;
        // sniping DPI max steps is 4, so one byte is plenty
        // this is hardcoded here as we can't read it from dilemma.c (private
        // config structure dilemma_config_t)
        (*command_data)[14] = 4;
        (*command_data)[15] = bkpd_get_auto_mouse_layer_enabled();
        (*command_data)[16] = bkpd_get_auto_precision_on_mouse_layer_enabled();
        (*command_data)[17] = bkpd_get_dragscroll_axis_invert_x();
        (*command_data)[18] = bkpd_get_dragscroll_axis_invert_y();
        // TODO dragscroll DPI
    }
#endif
}

/** \brief Return the current value of the pointer's default DPI. */
uint16_t bkpd_get_pointer_default_dpi(void) {
    // TODO: delete this whole function, move it into a generic getter for the webapp to read mode config one by one
    // return (uint16_t)g_bkpd_config.pointer_default_dpi * bkpd_get_default_dpi_config_step() + bkpd_get_minimum_default_dpi();
    return 0;
}

/** \brief Set the current value of the pointer's default DPI. */
void bkpd_set_pointer_default_dpi(uint16_t new_dpi) {
    // // new dpi is on 2 bytes:
    // // get the old DPI:
    // uint16_t old_dpi = bkpd_get_pointer_default_dpi();
    // // calculate the difference:
    // int16_t difference = new_dpi - old_dpi;
    // // calculate how many steps we need, it could be negative
    // uint16_t default_dpi_config_step = bkpd_get_default_dpi_config_step();
    // int16_t  new_steps               = difference / default_dpi_config_step;
    // // apply the steps one by one
    // bool forward = new_steps > 0;
    // for (int i = 0; i < abs(new_steps); i++) {
    //     bkpd_cycle_pointer_default_dpi(forward);
    // }
    
    // TODO: delete this whole function, move it into a generic setter for the webapp to set mode config
}

/** \brief Return the current value of the pointer's sniper-mode DPI. */
uint16_t bkpd_get_pointer_sniping_dpi(void) {
    // TODO: delete this whole function, move it into a generic getter for the webapp to read mode config one by one
    // return (uint16_t)g_bkpd_config.pointer_sniping_dpi * bkpd_get_sniping_dpi_config_step() + bkpd_get_minimum_sniping_dpi();
    return 0;
}

void bkpd_set_pointer_sniping_dpi(uint16_t new_dpi) {
    // TODO: delete this whole function, move it into a generic setter for the webapp to set mode config
    // // get the old DPI:
    // uint16_t old_dpi = bkpd_get_pointer_sniping_dpi();
    // // calculate the difference:
    // int16_t difference = new_dpi - old_dpi;
    // // calculate how many steps we need, it could be negative
    // uint16_t sniping_dpi_config_step = bkpd_get_sniping_dpi_config_step();
    // int8_t   new_steps               = difference / sniping_dpi_config_step;
    // // apply the steps one by one
    // bool forward = new_steps > 0;
    // for (int i = 0; i < abs(new_steps); i++) {
    //     bkpd_cycle_pointer_sniping_dpi(forward);
    // }
}

/** \brief Set the appropriate DPI for the input config. */
// TODO get rid of this function
static void bkpd_maybe_update_cpi(void) {
    // if (g_bkpd_config.is_dragscroll_enabled) {
    //     pointing_device_set_cpi(BK_POINTING_DEVICE_DRAGSCROLL_DPI);
    // } else if (g_bkpd_config.is_sniping_enabled) {
    //     pointing_device_set_cpi(bkpd_get_pointer_sniping_dpi());
    // } else {
    //     pointing_device_set_cpi(bkpd_get_pointer_default_dpi());
    // }
}

/**
 * \brief Update the pointer's default DPI to the next or previous step.
 *
 * Increases the DPI value if `forward` is `true`, decreases it otherwise.
 * The increment/decrement steps are equal to BK_BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP.
 */
// TODO get rid of this function
void bkpd_cycle_pointer_default_dpi_noeeprom(bool forward) {
    // g_bkpd_config.pointer_default_dpi += forward ? 1 : -1;
    // bkpd_maybe_update_cpi();
}

void bkpd_cycle_pointer_default_dpi(bool forward) {
    bkpd_cycle_pointer_default_dpi_noeeprom(forward);
    write_bkpd_config_to_eeprom();
}

/**
 * \brief Update the pointer's sniper-mode DPI to the next or previous step.
 *
 * Increases the DPI value if `forward` is `true`, decreases it otherwise.
 * The increment/decrement steps are equal to BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP.
 */
// TODO get rid of this function
void bkpd_cycle_pointer_sniping_dpi_noeeprom(bool forward) {
    // g_bkpd_config.pointer_sniping_dpi += forward ? 1 : -1;
    // bkpd_maybe_update_cpi();
}

// TODO get rid of this function
void bkpd_cycle_pointer_sniping_dpi(bool forward) {
    // bkpd_cycle_pointer_sniping_dpi_noeeprom(forward);
    // write_bkpd_config_to_eeprom();
}

bool bkpd_get_pointer_sniping_enabled(void) {
    // return g_bkpd_config.is_sniping_enabled;
    // TODO
    return 0;
}

void bkpd_set_pointer_sniping_enabled(bool enable) {
    // g_bkpd_config.is_sniping_enabled = enable;
    // bkpd_maybe_update_cpi();
    // TODO
}

void bkpd_set_auto_mouse_layer_enabled(bool enabled) {
    g_bkpd_config.auto_mouse_layer_enabled = enabled;
    set_auto_mouse_enable(enabled);
    write_bkpd_config_to_eeprom();
}

void bkpd_set_auto_precision_on_mouse_layer_enabled(bool enabled) {
    g_bkpd_config.auto_precision_on_mouse_layer_enabled = enabled;
    bkpd_maybe_update_cpi();
}

bool bkpd_get_auto_mouse_layer_enabled(void) {
    return g_bkpd_config.auto_mouse_layer_enabled;
}

bool bkpd_get_auto_precision_on_mouse_layer_enabled(void) {
    return g_bkpd_config.auto_precision_on_mouse_layer_enabled;
}

// TODO: remove this function
bool bkpd_get_pointer_dragscroll_enabled(void) {
    // return g_bkpd_config.is_dragscroll_enabled;
    // TODO
    return 0;
}

// TODO: remove this function
void bkpd_set_pointer_dragscroll_enabled(bool enable) {
    // g_bkpd_config.is_dragscroll_enabled = enable;
    // TODO
    // bkpd_maybe_update_cpi();
}

// TODO: remove this function
void bkpd_set_pointer_cursor_enabled(bool enable) {
    // g_bkpd_config.is_cursor_enabled = enable;
}

// TODO remove all this stuff into generic
uint16_t bkpd_get_minimum_default_dpi(void) {
    // return BK_POINTING_DEVICE_MINIMUM_DEFAULT_DPI;
    // TODO
    return 0;
}

uint16_t bkpd_get_default_dpi_config_step(void) {
    // return BK_POINTING_DEVICE_DEFAULT_DPI_CONFIG_STEP;
    // TODO
    return 0;
}

uint16_t bkpd_get_minimum_sniping_dpi(void) {
    // return BK_POINTING_DEVICE_MINIMUM_SNIPING_DPI;
    // TODO
    return 0;
}

uint16_t bkpd_get_sniping_dpi_config_step(void) {
    // return BK_POINTING_DEVICE_SNIPING_DPI_CONFIG_STEP;
    // TODO
    return 0;
}

/**
 * \brief Implement drag-scroll.
 */
report_mouse_t pointing_device_task_bk_pointing_device(report_mouse_t mouse_report) {
    if (is_keyboard_master()) {
        mouse_report = bkpd_process_active_mode(mouse_report);
        mouse_report = pointing_device_task_user(mouse_report);
    }
    return mouse_report;
}

// TODO missing && !NO_DILEMMA_KEYCODES?
//  #    if defined(BK_POINTING_DEVICE_ENABLE) && !defined(NO_BK_POINTING_DEVICE_KEYCODES)
/** \brief Whether SHIFT mod is enabled. */
static bool has_shift_mod(void) {
#ifdef NO_ACTION_ONESHOT
    return mod_config(get_mods()) & MOD_MASK_SHIFT;
#else
    return mod_config(get_mods() | get_oneshot_mods()) & MOD_MASK_SHIFT;
#endif // NO_ACTION_ONESHOT
}
//  #    endif // BK_POINTING_DEVICE_ENABLE && !NO_BK_POINTING_DEVICE_KEYCODES

void bkpd_activate_mode_if_pressed_normal_otherwise(keyrecord_t *record, uint8_t mode) {
    if (record->event.pressed) {
        bkpd_mode_set_active(mode);
    } else {
        bkpd_mode_set_active(MODE_NORMAL);
    }
}

/**
 * \brief Process keycodes related to the pointing device.
 */
bool process_record_bk_pointing_device(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record)) {
        return false;
    }

    switch (keycode) {
        // Generic and legacy ----------------------------------------
        case DPI_MOD:
            if (record->event.pressed) {
                // DPI change for all modes
                // Step backward if shifted, forward otherwise.
                changing_dpi_settings_for_mode = bkpd_mode_get_active_id();
                bkpd_mode_current_cycle_dpi(/* forward= */ !has_shift_mod());
            }
            break;
        case DPI_RMOD:
            if (record->event.pressed) {
                // Step forward if shifted, backward otherwise.
                changing_dpi_settings_for_mode = bkpd_mode_get_active_id();
                bkpd_mode_current_cycle_dpi(/* forward= */ has_shift_mod());
            }
            break;
        case S_D_MOD: // legacy
            if (record->event.pressed) {
                // Step backward if shifted, forward otherwise.
                changing_dpi_settings_for_mode = MODE_SNIPING;
                bkpd_mode_legacy_sniping_cycle_dpi(/* forward= */ !has_shift_mod());
            }
            break;
        case S_D_RMOD: // legacy
            if (record->event.pressed) {
                // Step forward if shifted, backward otherwise.
                changing_dpi_settings_for_mode = MODE_SNIPING;
                bkpd_mode_legacy_sniping_cycle_dpi(/* forward= */ has_shift_mod());
            }
            break;
        case INVX:
            bkpd_mode_current_invert_axis(/* axis= */ 1);
            break;
        case INVY:
            bkpd_mode_current_invert_axis(/* axis= */ 0);
            break;
            
        // Sniping mode ----------------------------------------
        case SNIPING:
            bkpd_activate_mode_if_pressed_normal_otherwise(record, MODE_SNIPING);
            break;
        case SNP_TOG:
            if (record->event.pressed) {
                bkpd_mode_toggle_active(MODE_SNIPING);
            }
            break;

        // Dragscroll mode ----------------------------------------
        case DRGSCRL:
            bkpd_activate_mode_if_pressed_normal_otherwise(record, MODE_DRAGSCROLL);
            break;
        case DRG_TOG:
            if (record->event.pressed) {
                bkpd_mode_toggle_active(MODE_DRAGSCROLL);
            }
            break;

        // Cursor mode ----------------------------------------
        case CURSOR:
            bkpd_activate_mode_if_pressed_normal_otherwise(record, MODE_CURSOR);
            break;
        case CUR_TOG:
            if (record->event.pressed) {
                bkpd_mode_toggle_active(MODE_CURSOR);
            }
            break;
        default:
            changing_dpi_settings_for_mode = -1; // reset
            break;
    }
    return true;
}

#if defined(POINTING_DEVICE_AUTO_MOUSE_ENABLE)
/**
 * Treat pointing-device keycodes as mouse keys so auto mouse layer stays
 * active while they are held.
 *
 * QMK deactivates the auto mouse layer on a non-mouse key unless something
 * else is holding it (movement, a held mouse key, or a layer toggle).
 * DRGSCRL/SNIPING are hold-to-enable; if the layer drops while they are
 * held, the key-up is resolved on layer 0 and the mode never turns off.
 */
// TODO test full range from introspection.h
bool is_mouse_record_kb(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case DPI_MOD:
        case DPI_RMOD:
        case S_D_MOD:
        case S_D_RMOD:
        case SNIPING:
        case SNP_TOG:
        case DRGSCRL:
        case DRG_TOG:
            return true;
        default:
            return is_mouse_record_user(keycode, record);
    }
}
#endif // POINTING_DEVICE_AUTO_MOUSE_ENABLE

/*
 *   \brief Manage a visual indicator of the DPI/Sniping DPI that's being changed.
 */
 // TODO
#if defined(RGBLIGHT_ENABLE) || defined(RGB_MATRIX_ENABLE)

bool bkpd_is_changing_dpi_settings(void) {
    return (changing_dpi_settings_for_mode >= 0);
}

bool rgb_matrix_indicators_advanced_bk_pointing_device(uint8_t led_min, uint8_t led_max) {
    const uint8_t layer = get_highest_layer(layer_state);

    if (layer != AUTO_MOUSE_DEFAULT_LAYER) {
        return true; // process further in parent function
    }

    if (changing_dpi_settings_for_mode >= 0) {
        // TODO move calculations here directly
        uint8_t  steps_per_led = 1;
        uint8_t  max_steps     = 0;
        uint8_t  current_step  = 0;
        uint16_t min_index     = LED_DPI_INDICATOR_INDEX;
        RGB      color         = {0, 255, 0};

        // handle brightness
        color.r = (color.r * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        color.g = (color.g * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        color.b = (color.b * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;

        max_steps     = g_bkpd_config.modes_config[changing_dpi_settings_for_mode].max_dpi_steps;
        current_step  = g_bkpd_config.modes_config[changing_dpi_settings_for_mode].current_dpi_step;

        // up to 15 LEDs MAX, otherwise we divide by 15
        if(max_steps > 15) {
            uint8_t modulo = max_steps / 15 + 1; // round up
            steps_per_led = modulo;
            max_steps = max_steps  / modulo + max_steps % 15;
        } else {
            steps_per_led = 1;
        }

        // max leds we will light, we divide by 2 otherwise it's a lot of LEDs
        for (int i = led_min; i < led_max; i++) {
            // TODO handle non-argos? (not really possible right now)
            // light up only the primary side.
            uint8_t index_symmetric = i % (RGBLIGHT_LED_COUNT / 2);

            // handle LEDs if we are in range of the display bar
            if (index_symmetric >= min_index && index_symmetric < min_index + max_steps) {
                // handle last step (could be full or half brightness)
                uint8_t last_step = min_index + current_step / steps_per_led;
                if (index_symmetric == last_step) {
                    // half brightness for odd DPI steps (2 steps/LED)
                    if (steps_per_led == 2 && current_step % steps_per_led == 0) {
                        rgb_matrix_set_color(i, (color.r + 255) / 2, color.g / 2, color.b / 2);
                    }
                    // full brightness for sniping DPI and even DPI steps
                    else {
                        rgb_matrix_set_color(i, color.r, color.g, color.b);
                    }
                }
                // handle LEDs before the current step that are still active
                else if (index_symmetric < last_step) {
                    rgb_matrix_set_color(i, color.r, color.g, color.b);
                }
                // handle other LEDs (not active but in display bar)
                else {
                    // default red
                    RGB red = {255, 0, 0};
                    // handle brightness
                    red.r = (red.r * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
                    red.g = (red.g * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
                    red.b = (red.b * rgb_matrix_get_val()) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
                    rgb_matrix_set_color(i, red.r, red.g, red.b);
                }
            }
            // turn off all other leds
            else {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
        }
        return false;
    }
    return true; // process further in parent function
}

#endif

/**
 * \brief Initialize the pointing device.
 * Manages memory space for Argos an non-Argos configuration.
 * Copies the defined invert x/y axis configuration into dynamic memory.
 */
void keyboard_post_init_bk_pointing_device(void) {
    read_bkpd_config_from_eeprom();
    bkpd_maybe_update_cpi();
    // initialize mode configs
    bkpd_modes_init();
    // TODO: replace with per-module memory management
#ifdef COMMUNITY_MODULE_ARGOS_ENABLE
#else
    eeconfig_init_user();
#endif
    set_auto_mouse_layer(AUTO_MOUSE_DEFAULT_LAYER);
    if (g_bkpd_config.auto_mouse_layer_enabled) {
        set_auto_mouse_enable(true);
    } else {
        set_auto_mouse_enable(false);
    }

    if (!g_bkpd_config.has_copied_qmk_config) {
        g_bkpd_config.has_copied_qmk_config = true;
        // TODO
// #ifdef BK_POINTING_DEVICE_DRAGSCROLL_REVERSE_X
//         g_bkpd_config.dragscroll_axis_invert_x = true;
// #endif
// #ifdef BK_POINTING_DEVICE_DRAGSCROLL_REVERSE_Y
//         g_bkpd_config.dragscroll_axis_invert_y = true;
// #endif
        write_bkpd_config_to_eeprom();
    }
}

/**
 * \brief Switch to precision mode on mouse layer if that option is enabled.
 */
layer_state_t layer_state_set_bk_pointing_device(layer_state_t state) {
    // this will automatically affect the DPI of the active mode if auto mouse layer is enabled
    bkpd_mode_current_affect_dpi();
    return state;
}

void bkpd_set_dragscroll_axis_invert_x(bool invert) {
    // g_bkpd_config.dragscroll_axis_invert_x = invert;
    // write_bkpd_config_to_eeprom();
    // TODO
}

void bkpd_set_dragscroll_axis_invert_y(bool invert) {
    // g_bkpd_config.dragscroll_axis_invert_y = invert;
    // write_bkpd_config_to_eeprom();
    // TODO
}

void bkpd_set_dragscroll_dpi(uint16_t dpi) {
    // TODO
    // g_bkpd_config.dragscroll_dpi = dpi;
    // write_bkpd_config_to_eeprom(&g_bkpd_config);
}

bool bkpd_get_dragscroll_axis_invert_x(void) {
    // return g_bkpd_config.dragscroll_axis_invert_x;
    // TODO
    return 0;
}

bool bkpd_get_dragscroll_axis_invert_y(void) {
    // return g_bkpd_config.dragscroll_axis_invert_y;
    // TODO
    return 0;
}

uint16_t bkpd_get_dragscroll_dpi(void) {
    // TODO
    return 0;
}

/**
 * \brief Auto mouse layer implementation for trackpads
 * We override the kb task, because QMK does not provide (as of coding this) a module-level override.
 */
#ifdef POINTING_DEVICE_DRIVER_digitizer
bool digitizer_task_kb(digitizer_t *const digitizer_state) {
    // TODO
//     report_mouse_t     report      = {0};
//     static digitizer_t last_report = {0};
//     uint16_t           delta_x     = 0;
//     uint16_t           delta_y     = 0;

//     if (!digitizer_task_user(digitizer_state)) {
//         return false;
//     }

//     for (int i = 0; i < DIGITIZER_CONTACT_COUNT; i++) {
// #    if DIGITIZER_FINGER_COUNT > 0
//         if (i < DIGITIZER_FINGER_COUNT) {
//             delta_x += digitizer_state->contacts[i].x - last_report.contacts[i].x;
//             delta_y += digitizer_state->contacts[i].y - last_report.contacts[i].y;
//         }
// #    endif
//     }

//     // "fake copy" it into the mouse report so that the auto mouse layer may trigger if needed
//     report.x = delta_x;
//     report.y = delta_y;
//     pointing_device_task_auto_mouse(report);

//     last_report = *digitizer_state; // copy the state to the last report

    // trigger a button state changed in master
    return true;
}

#endif