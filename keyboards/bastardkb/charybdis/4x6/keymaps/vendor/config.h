/**
 * Copyright 2021 Charly Delay <charly@codesink.dev> (@0xcharly)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
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

/*
 * Flash size.
 *
 * These boards carry a 4 MB QSPI flash, but the platform default
 * (`PICO_FLASH_SIZE_BYTES`, from the pico-sdk `pico` board header pulled in by
 * ChibiOS's pico bindings) is 2 MB. That single value is used in three places
 * and they must all agree:
 *   - wear-leveling derives `WEAR_LEVELING_RP2040_FLASH_SIZE` (and therefore
 *     `WEAR_LEVELING_RP2040_FLASH_BASE`) from it; otherwise the EEPROM region
 *     sits at 0x1C0000 and the top half of the chip is wasted;
 *   - pico-sdk's `flash_range_erase`/`program` `hard_assert` the target range
 *     within it, and a mismatch calls `panic()` -> `chSysHalt()` at boot;
 *   - the linker flash region is set separately via
 *     `-Wl,--defsym=FLASH_LEN=0x400000` in this keymap's `rules.mk`.
 *
 * NOTE: this moves the EEPROM region (to 0x3C0000), so existing settings
 * (including the VIA dynamic-keymap layout) are discarded once and
 * re-initialised.
 */
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)

#ifdef VIA_ENABLE
/* VIA configuration. */
#    define DYNAMIC_KEYMAP_LAYER_COUNT 4
#endif // VIA_ENABLE

#ifndef __arm__
/* Disable unused features. */
#    define NO_ACTION_ONESHOT
#endif // __arm__

#ifdef AUTO_MOUSE_DEFAULT_LAYER
#    undef AUTO_MOUSE_DEFAULT_LAYER
#endif
#define AUTO_MOUSE_DEFAULT_LAYER 3

#ifdef LED_DPI_INDICATOR_INDEX
#    undef LED_DPI_INDICATOR_INDEX
#endif
#define LED_DPI_INDICATOR_INDEX 1 // accomodate invisible 2 first LEDs for easier DIY build debugging

#ifdef RGBLIGHT_LED_COUNT
#    undef RGBLIGHT_LED_COUNT
#endif
#define RGBLIGHT_LED_COUNT 56

/* Charybdis-specific features. */

#ifdef POINTING_DEVICE_ENABLE
// Automatically enable the pointer layer when moving the trackball.  See also:
// - `CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_TIMEOUT_MS`
// - `CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_THRESHOLD`
// #define CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_ENABLE
#endif // POINTING_DEVICE_ENABLE





























#undef BKSF_TEST
#define BKSF_TEST BKSF_TEST_DUMP








