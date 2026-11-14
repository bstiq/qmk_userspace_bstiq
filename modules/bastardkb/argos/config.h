// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE_ID 0x61

#ifdef EXTRA_EXTRA_LONG_COMBOS
#undef EXTRA_EXTRA_LONG_COMBOS
#endif

#ifdef EXTRA_SHORT_COMBOS
#undef EXTRA_SHORT_COMBOS
#endif

#ifdef EXTRA_LONG_COMBOS
#undef EXTRA_LONG_COMBOS
#endif

// enables custom global combo term by Argos
#ifndef COMBO_TERM_PER_COMBO
#define COMBO_TERM_PER_COMBO
#endif

#define FLOW_TAP_TERM 150

// enables custom global tapping term by Argos
#ifndef TAPPING_TERM_PER_KEY
#define TAPPING_TERM_PER_KEY
#endif

#ifndef QUICK_TAP_TERM_PER_KEY
#define QUICK_TAP_TERM_PER_KEY
#endif

// Macro text delay, otherwise keys can overwrite themselves and skip
#undef DYNAMIC_KEYMAP_MACRO_DELAY
#define DYNAMIC_KEYMAP_MACRO_DELAY 8

// More available live storage! Useful for tap dances and RGB
// Also, better read write endurance
#define WEAR_LEVELING_LOGICAL_SIZE 65536
#define WEAR_LEVELING_BACKING_SIZE (WEAR_LEVELING_LOGICAL_SIZE * 4)

// Keep the keyboard-specific transaction layout identical on both halves.
// The duck module may be present only in the master image while the initial
// clone is running; omitting its reserved ID on the secondary would shift
// PUT_BK_SPLIT_FLASH and make the master's clone transaction invalid there.
// PUT_BK_SPLIT_FLASH as last so it's always the same, regardless of wether we add more QMK features later
// (otherwise newer versions of firmware won't be able to flash older ones)
#define SPLIT_TRANSACTION_IDS_KB RPC_ID_RGB_SYNC, RPC_ID_LED_MATRIX_SYNC, PUT_BK_SPLIT_FLASH = 25

#if defined(POINTING_DEVICE_DRIVER_pmw3360) || defined(POINTING_DEVICE_DRIVER_digitizer) || defined(POINTING_DEVICE_DRIVER_cirque_pinnacle_spi)
    #define BK_HAS_POINTING_DEVICE 1
#endif

#ifndef SPLIT_LAYER_STATE_ENABLE
#define SPLIT_LAYER_STATE_ENABLE
#endif
#ifndef SPLIT_LED_STATE_ENABLE
#define SPLIT_LED_STATE_ENABLE
#endif

// Held-modifier names are drawn on the panel half, which needn't be the USB half.
#ifndef SPLIT_MODS_ENABLE
#    define SPLIT_MODS_ENABLE
#endif
