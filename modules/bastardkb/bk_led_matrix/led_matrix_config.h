/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
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

#include "quantum.h"

/* Settings changed at runtime over Argos (command 0x1B): the trackball
 * animation, the dim and off timeouts and what happens after the ball stops.
 * Argos' LED matrix storage: byte 0 the animation (led_matrix_motion.c), then
 * off and dim seconds, then the idle value + 1. */

#define BKLM_TIMEOUT_MAX_S 120

/* The 0x1B children: [0x90][0x1B][child][value]. */
enum bklm_command_id {
    bklm_cmd_get             = 0x01, /* reply: style, off s, dim s, idle, rotation (2), existing (2) */
    bklm_cmd_set_style       = 0x02,
    bklm_cmd_set_off_timeout = 0x03,
    bklm_cmd_set_dim_timeout = 0x04,
    bklm_cmd_save            = 0x05, /* writes everything but the style, which saves itself */
    bklm_cmd_set_idle        = 0x06,
    bklm_cmd_set_cycle       = 0x07, /* [low byte, high byte] of the rotation */
};

/* Seconds, 1..BKLM_TIMEOUT_MAX_S, or 0 for LED_MATRIX_MODULE_OFF_MS / _DIM_MS.
 * Each is checked on its own: a dim at or past off means the panel never dims. */
extern uint8_t bklm_off_s, bklm_dim_s;

/* The idle value: 0 the duck once the ball stops, 1-253 the animation keeps
 * drifting that many seconds first, 255 it drifts forever. Kept as value + 1,
 * 255 as itself, and 0 for LED_MATRIX_MODULE_MOTION_IDLE / _IDLE_MS. */
extern uint8_t bklm_idle_cfg;

/* The animations LM_ANIM steps through, a bit per style (bklm_cycle_bit), or
 * 0 for the compiled cycle (LED_MATRIX_MODULE_MOTION_CYCLE, or all of them). */
extern uint16_t bklm_cycle_mask;

/* Bumped by every save; the sync carries it so the panel half saves too. */
extern uint8_t bklm_save_seq;

/* Reads the saved settings; anything out of range reads as 0. */
void bklm_config_load(void);

/* Set one timeout for now, without saving. Out of range: false, unchanged. */
bool bklm_timeouts_set_off(uint8_t s);
bool bklm_timeouts_set_dim(uint8_t s);

/* The panel half takes the USB half's values; a timeout of 0 is valid here,
 * anything else out of range is ignored. */
void bklm_config_adopt(uint8_t off_s, uint8_t dim_s, uint8_t idle_cfg, uint16_t cycle_mask);

void bklm_config_save(void);

/* The timeouts in effect, in ms. */
uint32_t bklm_off_ms(void);
uint32_t bklm_dim_ms(void);

/* The timeouts in effect, in whole seconds, capped at 255, as GET reports them. */
uint8_t bklm_off_report_s(void);
uint8_t bklm_dim_report_s(void);

/* After the ball stops: whether the animation keeps drifting, and for how
 * long in ms (0: forever). */
bool     bklm_idle_enabled(void);
uint32_t bklm_idle_ms(void);

/* The idle value in effect, as GET reports it. */
uint8_t bklm_idle_report(void);

/* A style's bit in bklm_cycle_mask: built-in styles 0-11, then keymap styles
 * 0-3 as bits 12-15. -1 for a style with no bit, which is always in rotation. */
int8_t bklm_cycle_bit(uint8_t style);

/* The bits of every style this build has, as GET reports them. */
uint16_t bklm_cycle_existing(void);

/* The rotation in effect as a mask, as GET reports it: never 0. */
uint16_t bklm_cycle_report(void);

/* command_data[0] is the child, the reply goes from command_data[1]. Returns
 * true when the report should be sent back: for every known child, even when
 * its value was ignored. */
bool bklm_dispatch_command(uint8_t *command_data);
