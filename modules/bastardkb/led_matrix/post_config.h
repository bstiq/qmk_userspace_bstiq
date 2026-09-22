// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * Second WS2812 chain. QMK's driver is limited to 256 LEDs and is already
 * used for the per-key matrix, so this strip is bit-banged on its own pin.
 *
 * Define LED_MATRIX_MODULE_PIN in the keyboard or keymap config
 * (this file runs after keymap config.h).
 */
#ifndef LED_MATRIX_MODULE_PIN
#    error "led_matrix module requires LED_MATRIX_MODULE_PIN"
#endif
#ifndef LED_MATRIX_MODULE_LED_COUNT
#    define LED_MATRIX_MODULE_LED_COUNT 192
#endif
/* A frame holds interrupts for ~6 ms, so it is paced instead of sent every loop. */
#ifndef LED_MATRIX_MODULE_REFRESH_MS
#    define LED_MATRIX_MODULE_REFRESH_MS 100
#endif
