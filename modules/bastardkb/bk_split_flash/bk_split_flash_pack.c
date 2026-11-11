// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <string.h>

#include "print.h"
#include "transactions.h"

#include "bk_split_flash.h"
#include "bk_split_flash_flash.h"
#include "bk_split_flash_proto.h"
#include "bk_split_flash_pack.h"

// T7.1: the bulk data plane. Image chunks travel in a dedicated, length-prefixed
// frame carried by a single split transaction on the module's own transaction id
// (see bk_split_flash_pack.h). This file owns the frame and the master-side
// sender; the slave-side receiver lives in bk_split_flash_rpc.c next to the
// receive state it drives.

#ifdef SPLIT_TRANSACTION_RPC

// The whole point of T7.1: the shared RPC buffer must now hold a bulk frame, not
// just the 32 B control message. Fail the build if the override did not take.
_Static_assert(RPC_M2S_BUFFER_SIZE >= BKSF_BULK_FRAME_SIZE, "RPC_M2S_BUFFER_SIZE must be large enough for a bulk frame");
_Static_assert(RPC_S2M_BUFFER_SIZE >= sizeof(bksf_bulk_ack_t), "RPC_S2M_BUFFER_SIZE must hold a bulk ack");

extern split_transaction_desc_t split_transaction_table[NUM_TOTAL_TRANSACTIONS];

void bksf_bulk_init(void) {
    // Repurpose the module's transaction for the data plane: the frame in one
    // direction, the small ack in the other. The control-plane RPC keeps using
    // the shared RPC buffers through the four-transaction RPC path, which sets
    // its own lengths at runtime, so the two do not collide.
    split_transaction_table[RPC_ID_BK_SPLIT_FLASH_CMD].initiator2target_buffer_size  = BKSF_BULK_FRAME_SIZE;
    split_transaction_table[RPC_ID_BK_SPLIT_FLASH_CMD].target2initiator_buffer_size = sizeof(bksf_bulk_ack_t);
}

bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n, uint32_t *rxp_off_out) {
    static bool corrupt_done = false;

    bksf_bulk_frame_t frame = {
        .magic  = BKSF_BULK_MAGIC,
        .offset = off,
        .len    = n,
        .crc    = bksf_crc32(data, n),
    };
    memcpy(frame.data, data, n);

    // T4.3 link-integrity injection: flip one payload byte *after* the CRC is
    // computed, so the receiver's recomputed CRC mismatches and it NACKs. Mode 1
    // corrupts the first attempt only (the retry then succeeds); mode 2 corrupts
    // every attempt (the master exhausts its retries and aborts without commit).
    // The target is the image offset `BKSF_CORRUPT_OFF`, so the test is
    // independent of the chunk size: corrupt whichever chunk contains it.
    if (BKSF_CORRUPT_OFF != 0u && off <= (uint32_t)BKSF_CORRUPT_OFF && (uint32_t)BKSF_CORRUPT_OFF < off + n && (BKSF_CORRUPT_MODE == 2 || !corrupt_done)) {
        frame.data[BKSF_CORRUPT_OFF - off] ^= 0x01u;
        corrupt_done = true;
    }

    bksf_bulk_ack_t ack = {0};
    bool            ok  = transport_execute_transaction(RPC_ID_BK_SPLIT_FLASH_CMD, &frame, BKSF_BULK_FRAME_SIZE, &ack, sizeof(ack));
    if (rxp_off_out != NULL) {
        *rxp_off_out = ack.rxp_off;
    }

    if (ok && ack.magic == BKSF_BULK_ACK_MAGIC && ack.status == BKSF_ST_OK) {
        return true;
    }
    // Idempotency: the receiver already advanced past this chunk, so a previous
    // ack was lost. rxp_off == off+n means the data is safely buffered.
    if (ok && ack.magic == BKSF_BULK_ACK_MAGIC && ack.rxp_off == (uint32_t)(off + n)) {
        uprintf("bksf: bulk off=%u already applied (reply lost), rxp_off=%u\n", (unsigned)off, (unsigned)ack.rxp_off);
        return true;
    }
    uprintf("bksf: bulk off=%u not acked ok=%u magic=0x%08X st=%u rxp_off=%u\n", (unsigned)off, (unsigned)ok, (unsigned)ack.magic, (unsigned)ack.status, (unsigned)ack.rxp_off);
    return false;
}

// T7.2: a module-level quiesce window around the bulk phase. The transport is
// master-initiated and single-threaded, so a bulk frame and a matrix/RPC
// transaction can never interleave on the wire; what the bulk phase must not do
// is let the module's own idle probes race it. This flag gates that activity and
// is cleared deterministically when the data phase ends.
static bool bulk_quiesced = false;

void bksf_bulk_quiesce(void) {
    if (!bulk_quiesced) {
        bulk_quiesced = true;
        uprintf("bksf: bulk phase begin (split traffic quiesced)\n");
    }
}

void bksf_bulk_resume(void) {
    if (bulk_quiesced) {
        bulk_quiesced = false;
        uprintf("bksf: bulk phase end (split traffic resumed)\n");
    }
}

bool bksf_bulk_is_quiesced(void) {
    return bulk_quiesced;
}

#else // !SPLIT_TRANSACTION_RPC

void bksf_bulk_init(void) {}

bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n, uint32_t *rxp_off_out) {
    (void)off;
    (void)data;
    (void)n;
    (void)rxp_off_out;
    return false;
}

void bksf_bulk_quiesce(void) {}
void bksf_bulk_resume(void) {}
bool bksf_bulk_is_quiesced(void) {
    return false;
}

#endif // SPLIT_TRANSACTION_RPC
