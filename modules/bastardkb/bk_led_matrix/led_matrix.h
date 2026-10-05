// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "led_matrix_display.h"

bool process_record_bk_led_matrix(uint16_t keycode, keyrecord_t *record);
void process_records_bk_led_matrix_note_last_input(uint16_t keycode, keyrecord_t *record);
