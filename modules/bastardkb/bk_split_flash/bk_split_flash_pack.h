// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// bk_split_flash -- T7 bulk data plane (master -> slave image chunks).
//
// The control plane (bk_split_flash_rpc.c) moves tiny, versioned messages in
// the shared split RPC buffer. The data plane has to move whole image chunks
// far faster than one 32 B RPC per 20 B of payload. It therefore carries each
// chunk in its own length-prefixed frame, delivered by a *single* split
// transaction (`transport_execute_transaction` on the module's own transaction
// id) instead of the four-transaction RPC path, so the 32 B RPC buffer no
// longer bounds the payload.
//
// The frame is self-describing (magic + length) and carries a CRC32 over its
// payload, so a corrupted frame is detected and NACKed without being written to
// staging. The receiver's position is returned in every ack, which lets the
// master resync after a lost reply (the same recovery the control plane uses).
//
// Sizing: the transport's transaction descriptor sizes are `uint8_t`, so a
// frame must fit in 255 B. `BKSF_BULK_CHUNK` is the payload budget; the frame
// adds a 16 B header. The chunk must also complete inside the driver's
// `SERIAL_USART_TIMEOUT` (20 ms): at 230400 10-bits/byte (~23040 B/s) 240 B takes
// ~10.4 ms, and at 460800 (the split link speed this board runs) ~5.2 ms, so
// `BKSF_BULK_CHUNK` 224 (240 B frame) is safe with margin either way. Throughput
// is wire-bound, so the serial baud dominates; the per-chunk header+ack cost
// (16 B header + 2 B handshake + 12 B ack) is only ~12% on top of the payload.

#define BKSF_BULK_MAGIC 0xB5F7B0C1u
#define BKSF_BULK_ACK_MAGIC 0xB5F7AC40u

#ifndef BKSF_BULK_CHUNK
#    define BKSF_BULK_CHUNK 224
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;  // BKSF_BULK_MAGIC
    uint32_t offset; // image offset of `data`
    uint32_t len;    // bytes used in `data` (the frame's length prefix)
    uint32_t crc;    // CRC32 over data[0..len-1]
    uint8_t  data[BKSF_BULK_CHUNK];
} bksf_bulk_frame_t;

#define BKSF_BULK_FRAME_SIZE ((uint32_t)sizeof(bksf_bulk_frame_t))

_Static_assert(BKSF_BULK_FRAME_SIZE <= 255, "bulk frame must fit the uint8_t transaction size field");

typedef struct __attribute__((packed)) {
    uint32_t magic;   // BKSF_BULK_ACK_MAGIC
    uint32_t rxp_off; // receiver's position after the frame (for resync)
    uint8_t  status;  // BKSF_ST_OK, or a BKSF_ST_* error
    uint8_t  pad[3];
} bksf_bulk_ack_t;

// Set up the module's own transaction for the bulk data plane (both halves).
// Called from bksf_rpc_init() after the RPC callback is registered.
void bksf_bulk_init(void);

// Master: send one image chunk and read its ack. Returns true on ACK, or on an
// idempotent "already applied" reply (a lost ack). `rxp_off_out`, if non-NULL,
// receives the receiver's position so the caller can resync on failure.
bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n, uint32_t *rxp_off_out);

// T7.2: quiesce the module's own normal split activity for the bulk phase and
// resume it deterministically afterwards. While quiesced the master's idle peer
// watchdog is suspended (it would otherwise probe the link mid-transfer) and no
// non-bulk module RPC is issued; the bulk phase itself is the only traffic.
void bksf_bulk_quiesce(void);
void bksf_bulk_resume(void);
bool bksf_bulk_is_quiesced(void);
