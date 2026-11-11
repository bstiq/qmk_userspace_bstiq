// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// bk_split_flash -- update both halves of an RP2040 split keyboard from the
// master (USB) half, with a boot2-CRC fail-safe that lands an interrupted
// update in RPI-RP2 BOOTSEL rather than a brick.
//
// Part 1 provides only the module skeleton, a trigger keycode, and read-only
// flash introspection. The fail-safe (ARM/COPY/COMMIT) and the split control
// plane land in later parts.

#ifndef BKSF_VERSION
#    define BKSF_VERSION "0.1.0"
#endif

// Update state, shared by the trigger, the indicator, and the split handshake.
typedef enum {
    BKSF_STATE_IDLE = 0,
    BKSF_STATE_PREPARE,
    BKSF_STATE_ARMED,
    BKSF_STATE_COPYING,
    BKSF_STATE_COMMITTING,
    BKSF_STATE_ERROR,
} bksf_state_t;

bksf_state_t bksf_get_state(void);

// Split control handshake (Part 3).
void bksf_rpc_init(void);        // register the module's RPC callback
void bksf_start_handshake(void); // master: begin ARM -> ARMED, driven by housekeeping

// Part 4: master streams the const peer image to the slave (ARM -> BEGIN ->
// chunks -> VERIFY -> COMMIT), driven by housekeeping.
void bksf_start_slave_update(void);

// Part 5 (T5.1): like bksf_start_slave_update(), but after the slave rejoins the
// master self-flashes the same peer image last (T2.5 path) and resets, so a
// single trigger updates both halves. Driven by housekeeping; does not return
// once the master's own apply begins.
void bksf_start_both_update(void);
