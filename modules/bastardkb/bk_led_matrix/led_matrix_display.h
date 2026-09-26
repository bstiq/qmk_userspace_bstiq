// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* Visual grid. Same size as the physical strip; y = 0 is the bottom row. */
#define BKLM_COLS 12
#define BKLM_ROWS 16

void bklm_draw_bitmap_glyph(RGB *pixels, const uint8_t *rows, uint8_t width, uint8_t height, uint8_t origin_x, uint8_t origin_y_top, RGB color);
void bklm_send_frame_to_strip(const RGB *pixels);
void bklm_set_strip_brightness(uint8_t brightness);
void bklm_reset_strip(void);
