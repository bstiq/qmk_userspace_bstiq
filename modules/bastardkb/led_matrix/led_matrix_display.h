// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* Paints one visual framebuffer and returns after the strip latches.
 * pixels has LED_MATRIX_MODULE_LED_COUNT entries, row-major, x = 0 at the
 * left and y = 0 at the bottom. No-op when pixels is NULL. */
void bklm_show(const RGB *pixels);

/* Stores the module brightness applied on the next bklm_show. 0 is off, 255 is full. */
void bklm_set_brightness(uint8_t brightness);

/* Drives the data pin low long enough for the strip to reset. */
void bklm_init(void);
