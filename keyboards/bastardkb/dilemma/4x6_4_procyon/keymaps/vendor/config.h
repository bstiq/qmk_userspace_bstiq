/**
 * Copyright 2021 Charly Delay <charly@codesink.dev> (@0xcharly)
 * Copyright 2023 casuanoob <casuanoob@hotmail.com> (@casuanoob)
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

/* Split serial link speed. Half-duplex PIO serial link. bk_split_flash streams the
 * whole peer image over it, so the transfer is wire-bound: the baud is the hard
 * ceiling. SELECT_SOFT_SERIAL_SPEED tops out at 460800, so SERIAL_USART_SPEED is
 * set directly. 625000 gives an exact PIO clock divider (125e6 / (8 * 625000) =
 * 25.0), so no divider dither; if it is flaky drop to 460800, if clean try 781250
 * (div 20). 921600 (div 16.95, dithered) was too fast for this cable. */
#define SERIAL_USART_SPEED 625000

#define DYNAMIC_KEYMAP_LAYER_COUNT 8
#define SPLIT_LAYER_STATE_ENABLE
#define SPLIT_LED_STATE_ENABLE

#define ENCODER_RESOLUTION 4

#ifdef AUTO_MOUSE_DEFAULT_LAYER
#undef AUTO_MOUSE_DEFAULT_LAYER
#endif
#define AUTO_MOUSE_DEFAULT_LAYER 3

#ifdef LED_DPI_INDICATOR_INDEX
#undef LED_DPI_INDICATOR_INDEX
#endif
#define LED_DPI_INDICATOR_INDEX 9

#ifdef RGBLIGHT_LED_COUNT
#undef RGBLIGHT_LED_COUNT
#endif
#define RGBLIGHT_LED_COUNT 104

/* bk_split_flash bring-up test selector (T4.3). The module config.h provides the
 * defaults, so override here with #undef + #define and rebuild:
 *   BKSF_TEST_RPC          - T3 ARM/ABORT handshake (arms the slave)   -> image E / case A1
 *   BKSF_TEST_UPDATE_SLAVE - T4 stream + commit the peer image         -> image F / cases A2,A3
 *   BKSF_TEST_DUMP         - facts only (default)
 * T4.3 link-integrity injection:
 *   BKSF_CORRUPT_OFF  - image offset of the chunk to corrupt (0 = off)
 *   BKSF_CORRUPT_MODE - 1 = transient (retry recovers, image G / B1)
 *                       2 = persistent (retries exhaust -> abort, image H / B2) */
#undef BKSF_TEST
#define BKSF_TEST BKSF_TEST_DUMP

/* Bring-up escape hatch: argos otherwise blocks *all* keys once the two halves
 * report different build ids (which is exactly what a half-flashed dev board
 * looks like, and what a completed T4 update leaves until the master is
 * reflashed too). Keep typing so the bring-up tests can run. Remove this line to
 * restore normal argos gating. */
#define ARGOS_DISABLE_BUILD_ID_GATE






