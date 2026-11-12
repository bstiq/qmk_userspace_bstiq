// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "print.h"

#include "bk_split_flash.h"
#include "bk_split_flash_flash.h"

// QMK platform hook (weak, defined by the RP2040 bootloader driver).
extern void mcu_reset(void);

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 1);

static bksf_state_t bksf_state = BKSF_STATE_IDLE;

// T9.2: ERROR is a transient indication -- shown briefly, then back to IDLE so a
// failed run does not leave the board painted forever. 0 = no auto-clear.
#ifndef BKSF_ERROR_HOLD_MS
#    define BKSF_ERROR_HOLD_MS 3000u
#endif
static uint32_t bksf_state_until = 0;

bksf_state_t bksf_get_state(void) {
    return bksf_state;
}

void bksf_set_state(bksf_state_t state) {
    bksf_state       = state;
    bksf_state_until = (state == BKSF_STATE_ERROR) ? (timer_read32() + BKSF_ERROR_HOLD_MS) : 0u;
}

void bksf_tick_state(void) {
    if (bksf_state == BKSF_STATE_ERROR && bksf_state_until != 0u && (int32_t)(timer_read32() - bksf_state_until) >= 0) {
        bksf_state       = BKSF_STATE_IDLE;
        bksf_state_until = 0u;
    }
}

// T9.2: "flash pending" indicator on the per-key/underglow RGB. Renders the
// shared update state; IDLE leaves the running effect untouched. bk_split_flash
// is last in the module list, so this runs after argos and overrides its colors
// while a run is in flight.
#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_advanced_bk_split_flash(uint8_t led_min, uint8_t led_max) {
    uint8_t r, g, b;
    switch (bksf_state) {
        case BKSF_STATE_PREPARE:
            r = 0xFF; g = 0xFF; b = 0xFF; // white
            break;
        case BKSF_STATE_ARMED:
            r = 0xFF; g = 0x60; b = 0x00; // amber
            break;
        case BKSF_STATE_COPYING:
            r = 0x00; g = 0x40; b = 0xFF; // blue
            break;
        case BKSF_STATE_COMMITTING:
            r = 0xA0; g = 0x00; b = 0xFF; // magenta
            break;
        case BKSF_STATE_ERROR:
            r = 0xFF; g = 0x00; b = 0x00; // red
            break;
        default:
            return true; // IDLE: leave the running effect alone
    }

    for (uint8_t i = led_min; i < led_max; i++) {
        rgb_matrix_set_color(i, r, g, b);
    }
    return true;
}
#endif

void keyboard_post_init_bk_split_flash(void) {
    keyboard_post_init_bk_split_flash_kb();

    // Register the module's split RPC callback (both halves).
    bksf_rpc_init();

    // Read-only introspection. Log the flash facts the fail-safe depends on so
    // they can be checked against the map file (T0.2). Emitted before the HID
    // console is attached, so it is usually only seen on the trigger press.
    bksf_dump_flash_facts();
}

static void bksf_run_test(void) {
    uprintf("bksf: %s test=%u\n", BKSF_VERSION, (unsigned)BKSF_TEST);
    bksf_dump_flash_facts();

#if BKSF_TEST == BKSF_TEST_ARM
    // T2.1: arm in place, stay running. Re-read the CRC word to confirm.
    bool ok = bksf_arm();
    uprintf("bksf: arm %s, crc now=0x%08X (armed=%u)\n", ok ? "OK" : "FAIL", (unsigned)bksf_boot2_crc_stored(), (unsigned)bksf_is_armed());
#elif BKSF_TEST == BKSF_TEST_BOOTSEL
    // T2.2: arm, then a *plain* reset so the ROM's CRC check (not reset_usb_boot)
    // is what decides. A valid boot2 would boot the app; a zeroed CRC drops to
    // RPI-RP2.
    bool ok = bksf_arm();
    uprintf("bksf: arm %s; plain reset -> expect RPI-RP2\n", ok ? "OK" : "FAIL");
    wait_ms(50);
    mcu_reset();
#elif BKSF_TEST == BKSF_TEST_COMMIT
    // T2.3: rewrite sector 0 identically, then reset. Must boot the same build.
    uprintf("bksf: commit identical sector 0; reset -> expect normal boot\n");
    wait_ms(50);
    bksf_commit_identical();
#elif BKSF_TEST == BKSF_TEST_UPDATE
    // T2.5: stage a distinct peer image, arm, apply (never returns). On success
    // the half reboots into the peer build; on interruption it lands in RPI-RP2.
    uprintf("bksf: self-update from peer image\n");
    bksf_self_update_from_peer();
#elif BKSF_TEST == BKSF_TEST_ERASE0
    // T2.4: leave sector 0 erased (as an interrupted COMMIT would), then reset.
    uprintf("bksf: leave sector 0 erased; reset -> expect RPI-RP2\n");
    wait_ms(50);
    bksf_leave_sector0_invalid(false);
#elif BKSF_TEST == BKSF_TEST_PARTIAL0
    // T2.4: leave sector 0 programmed with zeros (a garbage boot2), then reset.
    uprintf("bksf: program sector 0 with zeros; reset -> expect RPI-RP2\n");
    wait_ms(50);
    bksf_leave_sector0_invalid(true);
#elif BKSF_TEST == BKSF_TEST_RPC
    // T3: master sends ARM, slave arms and replies ARMED (housekeeping drives it).
    bksf_start_handshake();
#elif BKSF_TEST == BKSF_TEST_UPDATE_SLAVE
    // T4: master streams the peer image into the slave's staging, verifies, and
    // the slave commits (reboots into the peer build). Housekeeping drives it.
    bksf_start_slave_update();
#elif BKSF_TEST == BKSF_TEST_UPDATE_BOTH
    // T5: update the slave first, then the master self-flashes the same peer
    // image last, so one trigger updates both halves. Housekeeping drives it.
    bksf_start_both_update();
#elif BKSF_TEST == BKSF_TEST_STAGING
    // T8.1: confirm a combined UF2's peer block landed at the staging address by
    // reading the staging region and comparing it to the embedded peer (size +
    // CRC). Read-only; nothing is written.
    bksf_verify_staging_peer();
#endif
}

bool process_record_bk_split_flash(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_bk_split_flash_kb(keycode, record)) {
        return false;
    }

    switch (keycode) {
        case BK_SPLIT_FLASH_TRIGGER:
            if (record->event.pressed) {
                bksf_run_test();
            }
            return false;
    }

    return true;
}
