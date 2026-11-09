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

#pragma once

// for rgb mostly
#define SPLIT_LAYER_STATE_ENABLE

// increase layer amount
#ifdef DYNAMIC_KEYMAP_LAYER_COUNT
#    undef DYNAMIC_KEYMAP_LAYER_COUNT
#endif
#define DYNAMIC_KEYMAP_LAYER_COUNT 8

#define ARGOS_OFFSET_CONFIG 0

#define ARGOS_SIZE_CONFIG 11 // manually sync with argos.h
// _Static_assert(sizeof(argos_config_t) == ARGOS_SIZE_CONFIG, "Invalid size for argos_config_t");

#define ARGOS_OFFSET_COMBO (ARGOS_OFFSET_CONFIG + ARGOS_SIZE_CONFIG)
#define ARGOS_COMBO_ENTRIES 24 // this was already defined in argos.h, TODO fix this hardcoding...
#define ARGOS_SIZE_COMBO 12

// If we modify the structure, we also need to modify its size in post_config.h
// _Static_assert(sizeof(argos_combo_t) == ARGOS_SIZE_COMBO, "Invalid size for argos_combo_t"); // this seems to big, change to 7 or 8?

#define ARGOS_SIZE_COMBOS (ARGOS_COMBO_ENTRIES * ARGOS_SIZE_COMBO)

#define ARGOS_OFFSET_TAP_DANCE (ARGOS_OFFSET_COMBO + ARGOS_SIZE_COMBOS)
#define ARGOS_TAP_DANCE_ENTRIES 50 // should be enough for anyone
#define ARGOS_SIZE_TAP_DANCE 11
// _Static_assert(sizeof(argos_td_entry_t) == ARGOS_SIZE_TAP_DANCE, "Invalid size for argos_td_entry_t");
#define ARGOS_SIZE_TAP_DANCES (ARGOS_TAP_DANCE_ENTRIES * ARGOS_SIZE_TAP_DANCE)

#define ARGOS_OFFSET_RGB_MATRIX (ARGOS_OFFSET_TAP_DANCE + ARGOS_SIZE_TAP_DANCES)

#ifndef RGBLIGHT_LED_COUNT
// fail compilation
#    error "RGBLIGHT_LED_COUNT is not defined"
#endif

// TODO remove hardcoded 10 layers max value
#define ARGOS_RGB_MATRIX_ENTRIES RGBLIGHT_LED_COUNT * 10 // up to 10 layers supported
#define ARGOS_SIZE_RGB_MATRIX_KEY_DATA 4
// _Static_assert(sizeof(argos_rgb_t) == ARGOS_SIZE_RGB_MATRIX_KEY_DATA, "Invalid size for argos_rgb_t");
#define ARGOS_SIZE_RGB_MATRIX_ENTRIES (ARGOS_RGB_MATRIX_ENTRIES * ARGOS_SIZE_RGB_MATRIX_KEY_DATA)

#define ARGOS_EEPROM_SIZE_CALC (ARGOS_SIZE_CONFIG + ARGOS_SIZE_COMBOS + ARGOS_SIZE_TAP_DANCES + ARGOS_SIZE_RGB_MATRIX_ENTRIES)

// Argos persists its data in its own community module eeprom datablock.
// The generated eeconfig_read_argos_datablock()/eeconfig_update_argos_datablock()
// APIs are exposed to the module code based on this size.
#define EECONFIG_MODULE_ARGOS_DATA_SIZE ARGOS_EEPROM_SIZE_CALC

// By default, macro space takes all the rest of the available space.
// This is made with AVR in mind, as that space will be very small.
// Here, it will be very big. This could cause issues with wear, since re-writing a macro
//    actually re-writes the whole macro space.
// So instead we define a smaller space manually.
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 16 * 512