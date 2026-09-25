// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

void keyboard_post_init_bk_led_matrix(void);
void housekeeping_task_bk_led_matrix(void);
void led_matrix_module_set_brightness(uint8_t brightness);
void led_matrix_module_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
/* Paint the 2 px gutter white on the given LED_MATRIX_DIR_*. No-op for NONE. */
void led_matrix_module_fill_band(uint8_t dir);
/* Clear the matrix and draw up to 9 characters. A-Z and 0-9 are shown; other bytes stay blank. */
void led_matrix_module_show_text(const char *text);
