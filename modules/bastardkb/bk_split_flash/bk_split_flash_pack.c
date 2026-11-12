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

// v4 bulk data plane. Image frames travel in a dedicated, length-prefixed frame
// carried by a single split transaction on the module's own transaction id.
// The slave-side receiver lives in bk_split_flash_rpc.c; it writes each frame
// straight into the RAM buffer (bk_split_flash_rx.c).

#ifdef SPLIT_TRANSACTION_RPC

// The shared RPC buffer must hold a whole frame; the control-plane reply must
// still fit its 32-byte message.
_Static_assert(RPC_M2S_BUFFER_SIZE >= BKSF_BULK_FRAME_SIZE, "RPC_M2S_BUFFER_SIZE must be large enough for a bulk frame");
_Static_assert(RPC_S2M_BUFFER_SIZE >= sizeof(bksf_msg_t), "RPC_S2M_BUFFER_SIZE must hold a control reply");

extern split_transaction_desc_t split_transaction_table[NUM_TOTAL_TRANSACTIONS];

void bksf_bulk_init(void) {
    // Frame in one direction, NO reply in the other: target2initiator size 0
    // skips the per-chunk ack wait. The control-plane RPC keeps using the shared
    // 32-byte buffers through its own four-transaction path (which sets its
    // lengths at runtime), so the two do not collide.
    split_transaction_table[RPC_ID_BK_SPLIT_FLASH_CMD].initiator2target_buffer_size  = BKSF_BULK_FRAME_SIZE;
    split_transaction_table[RPC_ID_BK_SPLIT_FLASH_CMD].target2initiator_buffer_size = 0;
}

// T4.3 link-integrity injection. Reset at the start of each run so mode 1
// ("corrupt the first attempt only") behaves identically across triggers.
static bool corrupt_done = false;

void bksf_bulk_reset(void) {
    corrupt_done = false;
}

bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n) {
    bksf_bulk_frame_t frame = {
        .magic  = BKSF_BULK_MAGIC,
        .offset = off,
        .len    = n,
        .crc    = bksf_crc32(data, n),
    };
    memcpy(frame.data, data, n);

    // Flip one payload byte *after* the CRC is computed, so the slave's
    // recomputed CRC mismatches and it drops the frame. Mode 1 corrupts the
    // first attempt only (the VERIFY bitmap flags that sector; on re-stream it
    // arrives clean); mode 2 corrupts every attempt (re-stream retries exhaust,
    // the master aborts and never commits). The target is the image offset
    // BKSF_CORRUPT_OFF, so the test is independent of the chunk size.
    if (BKSF_CORRUPT_OFF != 0u && off <= (uint32_t)BKSF_CORRUPT_OFF && (uint32_t)BKSF_CORRUPT_OFF < off + n && (BKSF_CORRUPT_MODE == 2 || !corrupt_done)) {
        frame.data[BKSF_CORRUPT_OFF - off] ^= 0x01u;
        corrupt_done = true;
    }

    // Fire-and-forget: target2initiator_length 0 -> no reply wait. The mandatory
    // 1-byte serial handshake still paces frames; correctness is guaranteed by
    // the final VERIFY bitmap, which re-streams any lost/corrupt sector.
    return transport_execute_transaction(RPC_ID_BK_SPLIT_FLASH_CMD, &frame, BKSF_BULK_FRAME_SIZE, NULL, 0);
}

// Quiesce window around the streaming phase: the transport is master-initiated
// and single-threaded, so a bulk frame and a matrix/RPC transaction can never
// interleave on the wire; this flag only gates the module's own idle probes.
static bool bulk_quiesced = false;

void bksf_bulk_quiesce(void) {
    if (!bulk_quiesced) {
        bulk_quiesced = true;
        uprintf("bksf: stream begin (split traffic quiesced)\n");
    }
}

void bksf_bulk_resume(void) {
    if (bulk_quiesced) {
        bulk_quiesced = false;
        uprintf("bksf: stream end (split traffic resumed)\n");
    }
}

bool bksf_bulk_is_quiesced(void) {
    return bulk_quiesced;
}

#else // !SPLIT_TRANSACTION_RPC

void bksf_bulk_init(void) {}
void bksf_bulk_reset(void) {}

bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n) {
    (void)off;
    (void)data;
    (void)n;
    return false;
}

void bksf_bulk_quiesce(void) {}
void bksf_bulk_resume(void) {}
bool bksf_bulk_is_quiesced(void) {
    return false;
}

#endif // SPLIT_TRANSACTION_RPC
