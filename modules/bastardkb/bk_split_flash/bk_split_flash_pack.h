// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// bk_split_flash -- v4 bulk data plane (master -> slave image frames).
//
// The control plane (bk_split_flash_rpc.c) moves tiny, versioned messages in
// the shared split RPC buffer. Image bytes travel in their own length-prefixed
// frame, delivered by a *single* split transaction on the module's own
// transaction id (see bk_split_flash_pack.c), so the 32 B RPC buffer no longer
// bounds the payload.
//
// v4 keeps one minimal per-frame reply (a 1-byte target2initiator_buffer_size).
// True fire-and-forget (zero reply) does not hold master/slave in lockstep on
// the half-duplex PIO serial link: the 1-byte handshake is sent before the
// payload is received/processed, so the slave must also answer *after* the
// callback. That reply makes each frame a proper request/reply transaction and
// paces the master to the slave's frame processing; it carries only a status
// byte (the master ignores it). Full correctness is guaranteed by the single
// final VERIFY (a per-sector bitmap), which re-streams any sector whose frame
// was lost or failed its CRC32. The streaming speed-up (no per-sector pacing,
// erase phase, flush polling, deaf IRQ-off windows) is unchanged.
//
// Sizing: the transport's transaction descriptor sizes are `uint8_t`, so a
// frame must fit in 255 B. `BKSF_BULK_CHUNK` is the payload budget; the frame
// adds a 16 B header. Frame layout within a 4 KiB sector is deterministic
// (BKSF_SLOTS_PER_SECTOR frames), which is what lets the slave build a compact
// per-sector bitmap instead of tracking every chunk globally.

#define BKSF_BULK_MAGIC 0xB5F7B0C1u

#ifndef BKSF_BULK_CHUNK
#    define BKSF_BULK_CHUNK 224
#endif
#ifndef BKSF_FLASH_SECTOR_SIZE
#    define BKSF_FLASH_SECTOR_SIZE 4096u
#endif

// Each 4 KiB sector is tiled by BKSF_SLOTS_PER_SECTOR frames, frame `k` starting
// at sector_base + k*BKSF_BULK_CHUNK. With the default 224 B chunk this is
// 18 full frames (224 B) + one 64 B tail = 19 slots.
#define BKSF_SLOTS_PER_SECTOR ((BKSF_FLASH_SECTOR_SIZE + BKSF_BULK_CHUNK - 1u) / BKSF_BULK_CHUNK)
_Static_assert(BKSF_SLOTS_PER_SECTOR <= 32u, "per-sector slot bitmask must fit a uint32_t");
#define BKSF_ALL_SLOTS_MASK ((BKSF_SLOTS_PER_SECTOR >= 32u) ? 0xFFFFFFFFu : ((1u << BKSF_SLOTS_PER_SECTOR) - 1u))

// Payload length of frame `slot` (all slots but the last carry BKSF_BULK_CHUNK).
static inline uint32_t bksf_slot_len(uint32_t slot) {
    uint32_t off = slot * BKSF_BULK_CHUNK;
    uint32_t rem = BKSF_FLASH_SECTOR_SIZE - off;
    return (rem < BKSF_BULK_CHUNK) ? rem : (uint32_t)BKSF_BULK_CHUNK;
}

typedef struct __attribute__((packed)) {
    uint32_t magic;  // BKSF_BULK_MAGIC
    uint32_t offset; // image offset of `data`
    uint32_t len;    // bytes used in `data`
    uint32_t crc;    // CRC32 over data[0..len-1]
    uint8_t  data[BKSF_BULK_CHUNK];
} bksf_bulk_frame_t;

#define BKSF_BULK_FRAME_SIZE ((uint32_t)sizeof(bksf_bulk_frame_t))
_Static_assert(BKSF_BULK_FRAME_SIZE <= 255, "bulk frame must fit the uint8_t transaction size field");

#define BKSF_BULK_REPLY_SIZE 1

// Set up the module's own transaction for the bulk data plane (both halves).
// Called from bksf_rpc_init() after the RPC callback is registered.
void bksf_bulk_init(void);

// Reset the link-integrity injection at the start of each run (master).
void bksf_bulk_reset(void);

// Master: push one image frame onto the wire. There is no ack/reply; a lost or
// corrupt frame is recovered by the final VERIFY bitmap. Returns true when the
// transaction (frame send) completed.
bool bksf_bulk_send_chunk(uint32_t off, const uint8_t *data, uint8_t n);

// Quiesce the module's normal idle split activity for the streaming window and
// resume it deterministically afterwards.
void bksf_bulk_quiesce(void);
void bksf_bulk_resume(void);
bool bksf_bulk_is_quiesced(void);
