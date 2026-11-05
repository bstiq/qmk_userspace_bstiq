/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
*  Copyright 2026 Ying Kun Zhan <ying@zhan.co.nl>
*  Copyright 2026 Henrik Pejer <mrpejer@gmail.com>
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

#include "led_matrix_display.h"

/* What the USB half sends the panel half (RPC_ID_LED_MATRIX_SYNC). Both halves
 * must run the same firmware: a change here needs both reflashed. */
typedef struct __attribute__((packed)) {
    uint8_t  mode;
    uint8_t  activity;
    uint8_t  style;        /* trackball animation, picked with LED_MATRIX_ANIMATION_NEXT */
    int16_t  dx, dy;       /* motion since the last delivered message */
    uint8_t  off_s, dim_s; /* bklm_off_s, bklm_dim_s */
    uint8_t  save_seq;     /* bklm_save_seq: a change means the USB half saved */
    uint8_t  idle_cfg;     /* bklm_idle_cfg */
    uint16_t cycle_mask;   /* bklm_cycle_mask */
} bklm_sync_t;

bool process_record_bk_led_matrix(uint16_t keycode, keyrecord_t *record);
void process_records_bk_led_matrix_note_last_input(uint16_t keycode, keyrecord_t *record);

/* The bk_pointing_device mode as seen by this half: local on the USB half,
 * synced from it on the other. */
uint8_t bklm_pointer_mode(void);
