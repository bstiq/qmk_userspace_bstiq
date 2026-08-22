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
#include "bk_pointing_rgb.h"

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

bkpd_config_t g_bkpd_config                  = {0};
int8_t changing_dpi_settings_for_mode = -1;

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

/* -----------------------------------------------------------------------------
            Argos command handling */

bool bkpd_dispatch_command(uint8_t *command_id, uint8_t *command_data) {
#if defined(COMMUNITY_MODULE_ARGOS_ENABLE)
    // We now switched to the command_id being argos_id_pointer.
    // However just in case, we will test for it and handle only if we got the correct command.
    if (*command_id == argos_id_pointer) {
        command_id   = &(command_data[0]);
        command_data = &(command_data[1]);
        switch (*command_id) {
            case argos_id_pointer_command_id_get_device_info: {
                bkpd_build_pointing_device_info_command_data(command_data);
                break;
            }
            case argos_id_pointer_command_id_get_mode_info: {
                uint8_t mode_id = command_data[0];
                command_data = &(command_data[1]);
                bkpd_build_mode_config_command_data(mode_id, command_data);
                break;
            }
            case argos_id_pointer_command_id_set_dpi: {
                uint8_t mode_id = command_data[0];
                uint16_t new_dpi = command_data[1] | (command_data[2] << 8);
                bkpd_mode_change_dpi(mode_id, new_dpi);
                break;
            }
            case argos_id_pointer_command_id_set_invert: {
                printf("command_id is argos_id_pointer_command_id_set_invert\n");
                uint8_t mode_id = command_data[0];
                uint8_t axis_index = command_data[1];
                bool invert = command_data[2];
                bkpd_mode_set_invert(mode_id, axis_index, invert);
                break;
            }
            case argos_id_pointer_command_id_set_auto_mouse_layer_enabled: {
                bool auto_mouse_layer_enabled = command_data[0];
                bkpd_set_auto_mouse_layer_enabled(auto_mouse_layer_enabled);
                break;
            }
            case argos_id_pointer_command_id_set_auto_precision_on_mouse_layer_enabled: {
                bool auto_precision_on_mouse_layer_enabled = command_data[0];
                bkpd_set_auto_precision_on_mouse_layer_enabled(auto_precision_on_mouse_layer_enabled);
                break;
            }
            default:
                break;
        }
    }
#endif
    return true; // always ack command
}

void bkpd_build_pointing_device_info_command_data(uint8_t *command_data) {
    command_data[0] = pointing_device_type_unknown;
#ifdef BK_HAS_POINTING_DEVICE
#    if defined(POINTING_DEVICE_DRIVER_pmw3360) // Charybdis / Dilemma trackball

    command_data[0] = pointing_device_type_trackball;
#    elif defined(POINTING_DEVICE_DRIVER_digitizer)           // Dilemma v3 / procyon
    command_data[0] = pointing_device_type_trackpad_procyon;
#    elif defined(POINTING_DEVICE_DRIVER_cirque_pinnacle_spi) // Dilemma v2 / cirque
    command_data[0] = pointing_device_type_trackpad_cirque;
#    endif
    if (command_data[0] != pointing_device_type_unknown) {
        for (uint8_t i = 1; i < 15; i++) {
            // legacy - set to 0.
            command_data[i] = 0;
        }
        command_data[15] = bkpd_get_auto_mouse_layer_enabled();
        command_data[16] = bkpd_get_auto_precision_on_mouse_layer_enabled();
    }
#endif
}

void bkpd_build_mode_config_command_data(uint8_t mode_id, uint8_t *command_data) {
#ifdef BK_HAS_POINTING_DEVICE
    // DPI
    uint16_t dpi       = bkpd_mode_get_dpi(mode_id);
    command_data[0] = dpi & 0xFF;
    command_data[1] = (dpi >> 8) & 0xFF;
    // Minimum DPI
    uint16_t minimum_dpi = bkpd_mode_get_minimum_dpi(mode_id);
    command_data[2]   = minimum_dpi & 0xFF;
    command_data[3]   = (minimum_dpi >> 8) & 0xFF;
    // DPI per step
    uint16_t dpi_per_step = bkpd_mode_get_dpi_per_step(mode_id);
    command_data[4]    = dpi_per_step & 0xFF;
    // MAX DPI
    uint16_t max_dpi   = bkpd_mode_get_max_dpi(mode_id);
    command_data[5] = max_dpi & 0xFF;
    command_data[6] = (max_dpi >> 8) & 0xFF;
    // Invert X axis
    command_data[7] = bkpd_mode_get_invert(mode_id, 0);
    // Invert Y axis
    command_data[8] = bkpd_mode_get_invert(mode_id, 1);
#endif
}

void bkpd_set_auto_mouse_layer_enabled(bool enabled) {
    g_bkpd_config.auto_mouse_layer_enabled = enabled;
    set_auto_mouse_enable(enabled);
    write_bkpd_config_to_eeprom();
}

void bkpd_set_auto_precision_on_mouse_layer_enabled(bool enabled) {
    g_bkpd_config.auto_precision_on_mouse_layer_enabled = enabled;
}

bool bkpd_get_auto_mouse_layer_enabled(void) {
    return g_bkpd_config.auto_mouse_layer_enabled;
}

bool bkpd_get_auto_precision_on_mouse_layer_enabled(void) {
    return g_bkpd_config.auto_precision_on_mouse_layer_enabled;
}

/**
 * \brief Implement pointing modes
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

/**
 * \brief Process keycodes related to the pointing device.
 */
bool process_record_bk_pointing_device(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record)) {
        return false;
    }

    // try to process trigger/toggle mode keycodes first
    uint8_t mode = bkpd_mode_from_keycode(keycode);
    if(mode != 255) {
        // figure out if we have a press or a toggle
        uint8_t min_keycode = bkpd_get_lowest_mode_keycode();
        // pressed keycode
        if((keycode - min_keycode) % 2 == 0) {
            if(record->event.pressed) {
                bkpd_mode_set_active(mode);
                printf("bkpd_mode_set_active: %d\n", mode);
            }
            else {
                printf("bkpd_mode_set_active: MODE_NORMAL\n");
                bkpd_mode_set_active(MODE_NORMAL);
            }
        } 
        // toggle keycode
        else {
            if(record->event.pressed) {
                bkpd_mode_toggle_active(mode);
            }
        }
        return true;
    }

    // process other keycodes
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

        // End pointer modes ----------------------------------------
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

/**
 * \brief Initialize the pointing device.
 * Manages memory space for Argos an non-Argos configuration.
 * Copies the defined invert x/y axis configuration into dynamic memory.
 */
void keyboard_post_init_bk_pointing_device(void) {
    read_bkpd_config_from_eeprom();
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
        write_bkpd_config_to_eeprom();
    }

    bkpd_mode_set_active(MODE_NORMAL);
}

/**
 * \brief Switch to precision mode on mouse layer if that option is enabled.
 */
layer_state_t layer_state_set_bk_pointing_device(layer_state_t state) {
    printf("layer_state_set_bk_pointing_device: state: %d, auto_precision_on_mouse_layer_enabled: %d\n", state, g_bkpd_config.auto_precision_on_mouse_layer_enabled);
    if (layer_state_cmp(state, AUTO_MOUSE_DEFAULT_LAYER) && \
            g_bkpd_config.auto_precision_on_mouse_layer_enabled) {
        bkpd_mode_set_active(MODE_SNIPING);
    } else { // in all other layers / mouse with no auto precision: normal DPI
        bkpd_mode_set_active(MODE_NORMAL);
    }
    return state;
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